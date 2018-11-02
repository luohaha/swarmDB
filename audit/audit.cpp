// Copyright (C) 2018 Bluzelle
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License, version 3,
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#include <audit/audit.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/format.hpp>

using namespace bzn;

audit::audit(std::shared_ptr<bzn::asio::io_context_base> io_context
        , std::shared_ptr<bzn::node_base> node
        , std::optional<boost::asio::ip::udp::endpoint> monitor_endpoint
        , bzn::uuid_t uuid
        , size_t mem_size
        , bool use_pbft)

        : uuid(std::move(uuid))
        , node(std::move(node))
        , io_context(std::move(io_context))
        , leader_alive_timer(this->io_context->make_unique_steady_timer())
        , leader_progress_timer(this->io_context->make_unique_steady_timer())
        , monitor_endpoint(std::move(monitor_endpoint))
        , socket(this->io_context->make_unique_udp_socket())
        , statsd_namespace_prefix("com.bluzelle.swarm.singleton.node." + this->uuid + ".")
        , mem_size(mem_size)
        , use_pbft(use_pbft)
{

}

const std::list<std::string>&
audit::error_strings() const
{
    return this->recorded_errors;
}

size_t
audit::error_count() const
{
    return this->recorded_errors.size() + this->forgotten_error_count;
}

void
audit::start()
{
    std::call_once(this->start_once, [this]()
    {
        this->node->register_for_message("audit", std::bind(&audit::handle
                                                            , shared_from_this()
                                                            , std::placeholders::_1
                                                            , std::placeholders::_2));
        if (this->monitor_endpoint)
        {
            LOG(info) << boost::format("Audit module running, will send stats to %1%:%2%")
                % this->monitor_endpoint->address().to_string()
                % this->monitor_endpoint->port();
        }
        else
        {
            LOG(info) "Audit module running, but not sending stats anywhere because no monitor configured";
        }
         
        LOG(info) << "Audit module running";


        if (this->use_pbft)
        {
            this->pbft_specific_init();
        }
        else
        {
            this->raft_specific_init();
        }
    });
}

void
audit::raft_specific_init()
{
    this->reset_leader_alive_timer();
}

void
audit::pbft_specific_init()
{
    this->reset_primary_alive_timer();
}

void
audit::reset_primary_alive_timer()
{
    LOG(debug) << "starting primary alive timer";
    this->primary_dead_count = 0;

    // We use the leader_alive_timer here because introducing a new timer would make the tests annoyingly complex
    this->leader_alive_timer->cancel();
    this->leader_alive_timer->expires_from_now(this->primary_timeout);
    this->leader_alive_timer->async_wait(std::bind(&audit::handle_primary_alive_timeout, shared_from_this(), std::placeholders::_1));
}

void
audit::reset_leader_alive_timer()
{
    LOG(debug) << "starting leader alive timer";
    this->leader_dead_count = 0;
    this->leader_alive_timer->cancel();
    this->leader_alive_timer->expires_from_now(this->leader_timeout);
    this->leader_alive_timer->async_wait(std::bind(&audit::handle_leader_alive_timeout, shared_from_this(), std::placeholders::_1));
}

void
audit::handle_leader_alive_timeout(const boost::system::error_code& ec)
{
    if (ec)
    {
        LOG(debug) << "Leader alive timeout canceled " << ec.message();
        return;
    }

    this->report_error(bzn::NO_LEADER_METRIC_NAME, str(boost::format("No leader alive [%1%]") % ++(this->leader_dead_count)));
    this->clear_leader_progress_timer();
    this->leader_has_uncommitted_entries = false;
    this->leader_alive_timer->expires_from_now(this->leader_timeout);
    this->leader_alive_timer->async_wait(std::bind(&audit::handle_leader_alive_timeout, shared_from_this(), std::placeholders::_1));
}

void
audit::handle_primary_alive_timeout(const boost::system::error_code& ec)
{
    if (ec)
    {
        LOG(debug) << "Primary alive timeout canceled " << ec.message();
        return;
    }

    this->report_error(bzn::NO_PRIMARY_METRIC_NAME, str(boost::format("No primary alive [%1%]") % ++(this->primary_dead_count)));
    this->leader_alive_timer->expires_from_now(this->primary_timeout);
    this->leader_alive_timer->async_wait(std::bind(&audit::handle_primary_alive_timeout, shared_from_this(), std::placeholders::_1));
}

void
audit::reset_leader_progress_timer()
{
    LOG(debug) << "(re)starting leader progress timer";
    this->leader_stuck_count = 0;
    this->leader_progress_timer->cancel();
    this->leader_progress_timer->expires_from_now(this->leader_timeout);
    this->leader_progress_timer->async_wait(std::bind(&audit::handle_leader_progress_timeout, shared_from_this(), std::placeholders::_1));
}

void
audit::clear_leader_progress_timer()
{
    this->leader_stuck_count = 0;
    this->leader_progress_timer->cancel();
}

void audit::handle_leader_progress_timeout(const boost::system::error_code& ec)
{
    if (ec)
    {
        LOG(debug) << "Leader progress timeout canceled " << ec.message();
        return;
    }

    this->report_error(bzn::LEADER_STUCK_METRIC_NAME, str(boost::format("Leader alive but not making progress [%1%]") % ++(this->leader_stuck_count)));
    this->leader_progress_timer->expires_from_now(this->leader_timeout);
    this->leader_progress_timer->async_wait(std::bind(&audit::handle_leader_progress_timeout, shared_from_this(), std::placeholders::_1));
}

void
audit::report_error(const std::string& metric_name, const std::string& description)
{
    this->recorded_errors.push_back(description);

    std::string metric = this->statsd_namespace_prefix + metric_name;

    LOG(fatal) << boost::format("[%1%]: %2%") % metric % description;
    this->send_to_monitor(metric + bzn::STATSD_COUNTER_FORMAT);

    this->trim();
}

void
audit::send_to_monitor(const std::string& stat)
{
    if (!this->monitor_endpoint)
    {
        return;
    }

    LOG(debug) << boost::format("Sending stat '%1%' to monitor at %2%:%3%")
                  % stat
                  % this->monitor_endpoint->address().to_string()
                  % this->monitor_endpoint->port();

    std::shared_ptr<boost::asio::const_buffer> buffer = std::make_shared<boost::asio::const_buffer>(stat.c_str(), stat.size());

    this->socket->async_send_to(*buffer, *(this->monitor_endpoint),
        [buffer](const boost::system::error_code& ec, std::size_t bytes)
        {
            if (ec)
            {
                LOG(error) << boost::format("UDP send failed, sent %1% bytes, '%2%'") %
                              ec.message() % bytes;
            }
        }
    );

}

void
audit::handle(const bzn::json_message& json, std::shared_ptr<bzn::session_base> session)
{
    audit_message message;
    message.ParseFromString(boost::beast::detail::base64_decode(json["audit-data"].asString()));

    LOG(debug) << "Got audit message" << message.DebugString();

    if (message.has_raft_commit())
    {
        this->handle_raft_commit(message.raft_commit());
    }
    else if (message.has_leader_status())
    {
        this->handle_leader_status(message.leader_status());
    }
    else if (message.has_pbft_commit())
    {
        this->handle_pbft_commit(message.pbft_commit());
    }
    else if (message.has_primary_status())
    {
        this->handle_primary_status(message.primary_status());
    }
    else if (message.has_failure_detected())
    {
        this->handle_failure_detected(message.failure_detected());
    }
    else
    {
        LOG(error) << "Got an unknown audit message? " << message.ShortDebugString();
    }

    session->close();
}

void audit::handle_primary_status(const primary_status& primary_status)
{
    if (!this->use_pbft)
    {
        LOG(debug) << "audit ignoring primary status message because we are in raft mode";
        return;
    }

    std::lock_guard<std::mutex> lock(this->audit_lock);

    if (this->recorded_primaries.count(primary_status.view()) == 0)
    {
        LOG(info) << "audit recording that primary of view " << primary_status.view() << " is '" << primary_status.primary() << "'";
        this->send_to_monitor(bzn::PRIMARY_HEARD_METRIC_NAME+bzn::STATSD_COUNTER_FORMAT);
        this->recorded_primaries[primary_status.view()] = primary_status.primary();
        this->trim();
    }
    else if (this->recorded_primaries[primary_status.view()] != primary_status.primary())
    {
        std::string err = str(boost::format(
                "Conflicting primary elected! '%1%' is the recorded primary of view %2%, but '%3%' claims to be the primary of the same view.")
                              % this->recorded_primaries[primary_status.view()]
                              % primary_status.view()
                              % primary_status.primary());
        this->report_error(bzn::PRIMARY_CONFLICT_METRIC_NAME, err);
    }

    this->reset_primary_alive_timer();
}

void
audit::handle_leader_status(const leader_status& leader_status)
{
    if (this->use_pbft)
    {
        LOG(debug) << "audit ignoring leader status message because we are in pbft mode";
        return;
    }

    std::lock_guard<std::mutex> lock(this->audit_lock);

    if (this->recorded_leaders.count(leader_status.term()) == 0)
    {
        LOG(info) << "audit recording that leader of term " << leader_status.term() << " is '" << leader_status.leader() << "'";
        this->send_to_monitor(bzn::NEW_LEADER_METRIC_NAME+bzn::STATSD_COUNTER_FORMAT);
        this->recorded_leaders[leader_status.term()] = leader_status.leader();
        this->trim();
    }
    else if (this->recorded_leaders[leader_status.term()] != leader_status.leader())
    {
        std::string err = str(boost::format(
                "Conflicting leader elected! '%1%' is the recorded leader of term %2%, but '%3%' claims to be the leader of the same term.")
                                             % this->recorded_leaders[leader_status.term()]
                                             % leader_status.term()
                                             % leader_status.leader());
        this->report_error(bzn::LEADER_CONFLICT_METRIC_NAME, err);
    }

    this->reset_leader_alive_timer();
    this->handle_leader_data(leader_status);
}

void
audit::handle_leader_data(const leader_status& leader_status)
{
    // The goal here is to implement a timer based on the leader making "progress" -
    // committing the uncommitted entries in its log. Specifically:
    // - The timer must be reset whenever the leader changes
    // - The timer must be halted when the leader has no uncommitted entries
    // - The timer must be reset, but not halted, when the leader commits some but not all of the uncommitted entries
    // - The timer must be restarted from full when the leader gets an uncommitted entry where it previously had none

    if (leader_status.leader() != this->last_leader)
    {
        // Leader has changed - halt or restart timer
        this->last_leader = leader_status.leader();
        this->handle_leader_made_progress(leader_status);
    }
    else if (leader_status.current_commit_index() > this->last_leader_commit_index)
    {
        // Leader has made progress - halt or restart timer
        this->handle_leader_made_progress(leader_status);
    }
    else if (leader_status.current_log_index() > leader_status.current_commit_index() && !this->leader_has_uncommitted_entries)
    {
        // Leader didn't have uncommitted entries, now it does - restart timer
        this->reset_leader_progress_timer();
        this->leader_has_uncommitted_entries = true;
    }
    this->last_leader_commit_index = leader_status.current_commit_index();
}

void audit::handle_leader_made_progress(const leader_status& leader_status)
{
    // Extracted common logic in the case where we know that the leader has made some progress,
    // but may or may not still have more messages to commit

    if (leader_status.current_commit_index() == leader_status.current_log_index())
    {
        this->clear_leader_progress_timer();
        this->leader_has_uncommitted_entries = false;
    }
    else
    {
        this->reset_leader_progress_timer();
        this->leader_has_uncommitted_entries = true;
    }
}

void
audit::handle_raft_commit(const raft_commit_notification& commit)
{
    std::lock_guard<std::mutex> lock(this->audit_lock);

    if (this->use_pbft)
    {
        LOG(debug) << "audit ignoring raft commit message because we are in pbft mode";
        return;
    }

    this->send_to_monitor(bzn::RAFT_COMMIT_METRIC_NAME + bzn::STATSD_COUNTER_FORMAT);

    if (this->recorded_raft_commits.count(commit.log_index()) == 0)
    {
        LOG(info) << "audit recording that message '" << commit.operation() << "' is committed at index " << commit.log_index();
        this->recorded_raft_commits[commit.log_index()] = commit.operation();
        this->trim();
    }
    else if (this->recorded_raft_commits[commit.log_index()] != commit.operation())
    {
        std::string err = str(boost::format(
                "Conflicting commit detected! '%1%' is the recorded entry at index %2%, but '%3%' has been committed with the same index.")
                              % this->recorded_raft_commits[commit.log_index()]
                              % commit.log_index()
                              % commit.operation());
        this->report_error(bzn::RAFT_COMMIT_CONFLICT_METRIC_NAME, err);
    }
}

void
audit::handle_pbft_commit(const pbft_commit_notification& commit)
{
    std::lock_guard<std::mutex> lock(this->audit_lock);

    if (!this->use_pbft)
    {
        LOG(debug) << "audit ignoring pbft commit message because we are in raft mode";
        return;
    }

    this->send_to_monitor(bzn::PBFT_COMMIT_METRIC_NAME + bzn::STATSD_COUNTER_FORMAT);

    if (this->recorded_pbft_commits.count(commit.sequence_number()) == 0)
    {
        LOG(info) << "audit recording that message '" << commit.request_hash() << "' is committed at sequence " << commit.sequence_number();
        this->recorded_pbft_commits[commit.sequence_number()] = commit.request_hash();
        this->trim();
    }
    else if (this->recorded_pbft_commits[commit.sequence_number()] != commit.request_hash())
    {
        std::string err = str(boost::format(
                "Conflicting commit detected! '%1%' is the recorded entry at sequence %2%, but '%3%' has been committed with the same sequence.")
                              % this->recorded_pbft_commits[commit.sequence_number()]
                              % commit.sequence_number()
                              % commit.request_hash());
        this->report_error(bzn::PBFT_COMMIT_CONFLICT_METRIC_NAME, err);
    }
}

void
audit::handle_failure_detected(const failure_detected& /*failure*/)
{
    // TODO KEP-539: more info in this message
    std::lock_guard<std::mutex> lock(this->audit_lock);

    if (!this->use_pbft)
    {
        LOG(debug) << "audit ignoring pbft failure detected message because we are in raft mode";
        return;
    }

    this->send_to_monitor(bzn::FAILURE_DETECTED_METRIC_NAME + bzn::STATSD_COUNTER_FORMAT);
}

size_t
audit::current_memory_size()
{
    return this->recorded_raft_commits.size() + this->recorded_errors.size() + this->recorded_leaders.size();
}

void
audit::trim()
{
    while(this->recorded_errors.size() > this->mem_size)
    {
        this->recorded_errors.pop_front();
        this->forgotten_error_count++;
    }

    // Here we're removing the lowest term/log index entries, which is sort of like the oldest entries. I'd rather
    // remove entries at random, but that's not straightforward to do with STL containers without making some onerous
    // performance compromise.

    while(this->recorded_leaders.size() > this->mem_size)
    {
        this->recorded_leaders.erase(this->recorded_leaders.begin());
    }

    while(this->recorded_raft_commits.size() > this->mem_size)
    {
        this->recorded_raft_commits.erase(this->recorded_raft_commits.begin());
    }
}
