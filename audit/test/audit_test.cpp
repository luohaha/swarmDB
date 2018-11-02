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
#include <mocks/mock_node_base.hpp>
#include <mocks/mock_boost_asio_beast.hpp>
#include <boost/range/irange.hpp>
#include <boost/asio/buffer.hpp>

using namespace ::testing;

class audit_test : public Test
{
public:
    std::shared_ptr<bzn::asio::Mockio_context_base> mock_io_context = std::make_shared<NiceMock<bzn::asio::Mockio_context_base>>();
    std::shared_ptr<bzn::Mocknode_base> mock_node = std::make_shared<NiceMock<bzn::Mocknode_base>>();

    std::unique_ptr<bzn::asio::Mocksteady_timer_base> leader_alive_timer =
            std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();
    std::unique_ptr<bzn::asio::Mocksteady_timer_base> leader_progress_timer =
            std::make_unique<NiceMock<bzn::asio::Mocksteady_timer_base>>();

    bzn::asio::wait_handler leader_alive_timer_callback;
    bzn::asio::wait_handler leader_progress_timer_callback;

    std::optional<boost::asio::ip::udp::endpoint> endpoint;
    std::unique_ptr<bzn::asio::Mockudp_socket_base> socket = std::make_unique<NiceMock<bzn::asio::Mockudp_socket_base>>();

    std::shared_ptr<bzn::audit> audit;

    size_t mem_size = 1000;

    bool use_pbft = false;

    audit_test()
    {
        // Here we are depending on the fact that audit.cpp will initialize the leader alive timer before the leader
        // progress timer. This is ugly, but it seems better than reaching inside audit to get the private timers or
        // call the private callbacks.
        EXPECT_CALL(*(this->mock_io_context), make_unique_steady_timer())
                .WillOnce(Invoke(
                [&](){return std::move(this->leader_alive_timer);}
        ))
                .WillOnce(Invoke(
                [&](){return std::move(this->leader_progress_timer);}
        ));

        EXPECT_CALL(*(this->leader_alive_timer), async_wait(_))
                .Times(AtLeast(1)) // This enforces the assumption: if this actually the leader progress timer,
                                   // then there are some tests that will never wait on it
                .WillRepeatedly(Invoke(
                [&](auto handler){this->leader_alive_timer_callback = handler;}
        ));

        EXPECT_CALL(*(this->leader_progress_timer), async_wait(_))
                .Times(AnyNumber())
                .WillRepeatedly(Invoke(
                [&](auto handler){this->leader_progress_timer_callback = handler;}
        ));

        EXPECT_CALL(*(this->mock_io_context), make_unique_udp_socket())
                .WillOnce(Invoke(
                [&](){return std::move(this->socket);}
        ));
    }

    void build_audit()
    {
        // We cannot construct this during our constructor because doing so invalidates our timer pointers,
        // which prevents tests from setting expectations on them
        this->audit = std::make_shared<bzn::audit>(this->mock_io_context, this->mock_node, this->endpoint, "audit_test_uuid", this->mem_size, this->use_pbft);
        this->audit->start();
    }

    bool progress_timer_running;
    uint progress_timer_reset_count;

    void progress_timer_status_expectations()
    {
        this->progress_timer_running = false;
        this->progress_timer_reset_count = 0;

        EXPECT_CALL(*(this->leader_progress_timer), cancel()).WillRepeatedly(Invoke(
                [&]()
                {
                    this->progress_timer_running = false;
                }
        ));

        EXPECT_CALL(*(this->leader_progress_timer), expires_from_now(_)).WillRepeatedly(Invoke(
                [&](auto /*time*/)
                {
                    this->progress_timer_running = true;
                    this->progress_timer_reset_count++;
                    return 0;
                }
        ));
    }

};

TEST_F(audit_test, test_timers_constructed_correctly)
{
    // This just tests the expecations built into the superclass
    this->build_audit();
}

TEST_F(audit_test, no_errors_initially)
{
    this->build_audit();
    EXPECT_EQ(this->audit->error_count(), 0u);
    EXPECT_EQ(this->audit->error_strings().size(), 0u);
}

TEST_F(audit_test, audit_throws_error_when_leaders_conflict)
{
    this->build_audit();
    leader_status a, b, c;

    a.set_leader("fred");
    a.set_term(1);

    b.set_leader("smith");
    b.set_term(2);

    c.set_leader("francheskitoria");
    c.set_term(1);

    this->audit->handle_leader_status(a);
    this->audit->handle_leader_status(b);
    this->audit->handle_leader_status(c);
    this->audit->handle_leader_status(b);

    EXPECT_EQ(this->audit->error_count(), 1u);
}

TEST_F(audit_test, audit_throws_error_when_primaries_conflict)
{
    this->use_pbft = true;
    this->build_audit();
    primary_status a, b, c;

    a.set_primary("fred");
    a.set_view(1);

    b.set_primary("smith");
    b.set_view(2);

    c.set_primary("francheskitoria");
    c.set_view(1);

    this->audit->handle_primary_status(a);
    this->audit->handle_primary_status(b);
    this->audit->handle_primary_status(c);
    this->audit->handle_primary_status(b);

    EXPECT_EQ(this->audit->error_count(), 1u);
}

TEST_F(audit_test, audit_throws_error_when_commits_conflict)
{
    this->build_audit();
    raft_commit_notification a, b, c;

    a.set_operation("do something");
    a.set_log_index(1);

    b.set_operation("do a different thing");
    b.set_log_index(2);

    c.set_operation("do something else");
    c.set_log_index(1);

    this->audit->handle_raft_commit(a);
    this->audit->handle_raft_commit(b);
    this->audit->handle_raft_commit(c);
    this->audit->handle_raft_commit(b);

    EXPECT_EQ(this->audit->error_count(), 1u);
}

TEST_F(audit_test, audit_throws_error_when_pbft_commits_conflict)
{
    this->use_pbft = true;
    this->build_audit();

    pbft_commit_notification a, b, c;

    a.set_request_hash("do something");
    a.set_sequence_number(1);

    b.set_request_hash("do a different thing");
    b.set_sequence_number(2);

    c.set_request_hash("do something else");
    c.set_sequence_number(1);

    this->audit->handle_pbft_commit(a);
    this->audit->handle_pbft_commit(b);
    this->audit->handle_pbft_commit(c);
    this->audit->handle_pbft_commit(b);

    EXPECT_EQ(this->audit->error_count(), 1u);
}

TEST_F(audit_test, audit_throws_error_when_no_leader_alive)
{
    EXPECT_CALL(*(this->leader_alive_timer), expires_from_now(_)).Times(AtLeast(1));
    this->build_audit();

    this->leader_alive_timer_callback(boost::system::error_code());
    EXPECT_EQ(this->audit->error_count(), 1u);
}

TEST_F(audit_test, audit_throws_error_when_no_primary_alive)
{
    this->use_pbft = true;
    EXPECT_CALL(*(this->leader_alive_timer), expires_from_now(_)).Times(AtLeast(1));
    this->build_audit();

    this->leader_alive_timer_callback(boost::system::error_code());
    EXPECT_EQ(this->audit->error_count(), 1u);
}

TEST_F(audit_test, audit_throws_error_when_leader_stuck)
{
    EXPECT_CALL(*(this->leader_progress_timer), expires_from_now(_)).Times(AtLeast(1));
    this->build_audit();

    leader_status ls;
    ls.set_leader("fred");
    ls.set_current_commit_index(6);
    ls.set_current_log_index(8);

    this->audit->handle_leader_status(ls);
    this->audit->handle_leader_status(ls);

    this->leader_progress_timer_callback(boost::system::error_code());
    EXPECT_EQ(this->audit->error_count(), 1u);
}

TEST_F(audit_test, audit_resets_leader_stuck_on_message)
{
    bool reset;
    EXPECT_CALL(*(this->leader_progress_timer), cancel()).WillRepeatedly(Invoke(
            [&](){reset = true;}
            ));

    this->build_audit();

    leader_status ls1;
    ls1.set_leader("fred");
    ls1.set_current_commit_index(6);
    ls1.set_current_log_index(8);

    leader_status ls2 = ls1;
    ls2.set_current_commit_index(7);

    this->audit->handle_leader_status(ls1);
    reset = false;
    this->audit->handle_leader_status(ls2);

    EXPECT_TRUE(reset);
    EXPECT_EQ(this->audit->error_count(), 0u);

    this->leader_progress_timer_callback(boost::system::error_code());

    EXPECT_EQ(this->audit->error_count(), 1u);
}

TEST_F(audit_test, audit_resets_leader_alive_on_message)
{
    bool reset;
    EXPECT_CALL(*(this->leader_alive_timer), cancel()).WillRepeatedly(Invoke(
            [&](){reset = true;}
    ));

    this->build_audit();

    leader_status ls1;
    ls1.set_leader("fred");
    ls1.set_current_commit_index(6);
    ls1.set_current_log_index(8);

    reset = false;
    this->audit->handle_leader_status(ls1);
    EXPECT_TRUE(reset);
    EXPECT_EQ(this->audit->error_count(), 0u);

    this->leader_alive_timer_callback(boost::system::error_code());

    EXPECT_EQ(this->audit->error_count(), 1u);

}

TEST_F(audit_test, audit_resets_primary_alive_on_message)
{
    bool reset;
    EXPECT_CALL(*(this->leader_alive_timer), cancel()).WillRepeatedly(Invoke(
            [&](){reset = true;}
    ));

    this->use_pbft = true;
    this->build_audit();

    primary_status ls1;
    ls1.set_primary("fred");

    reset = false;
    this->audit->handle_primary_status(ls1);
    EXPECT_TRUE(reset);
    EXPECT_EQ(this->audit->error_count(), 0u);

    this->leader_alive_timer_callback(boost::system::error_code());

    EXPECT_EQ(this->audit->error_count(), 1u);

}
TEST_F(audit_test, audit_no_error_or_timer_when_leader_idle)
{
    this->progress_timer_status_expectations();
    this->build_audit();

    leader_status ls1;
    ls1.set_leader("fred");
    ls1.set_current_commit_index(6);
    ls1.set_current_log_index(8);

    leader_status ls2 = ls1;
    ls2.set_current_commit_index(8);

    EXPECT_FALSE(this->progress_timer_running);
    this->audit->handle_leader_status(ls1);
    EXPECT_TRUE(this->progress_timer_running);
    this->audit->handle_leader_status(ls2);
    EXPECT_FALSE(this->progress_timer_running);

    EXPECT_EQ(this->audit->error_count(), 0u);
}

TEST_F(audit_test, audit_no_error_when_leader_livelock)
{
    // The case here is when new leaders keep getting elected, but none of them can make any progress
    // We would like to throw an error here, but detecting it properly is complex
    this->progress_timer_status_expectations();
    this->build_audit();

    leader_status ls1;
    ls1.set_leader("fred");
    ls1.set_current_commit_index(6);
    ls1.set_current_log_index(8);

    leader_status ls2;
    ls2.set_leader("joe");
    ls2.set_term(ls1.term()+1);
    ls2.set_current_commit_index(9);
    ls2.set_current_log_index(12);

    EXPECT_FALSE(this->progress_timer_running);

    this->audit->handle_leader_status(ls1);
    EXPECT_TRUE(this->progress_timer_running);

    uint old_count = this->progress_timer_reset_count;
    this->audit->handle_leader_status(ls2);
    EXPECT_TRUE(this->progress_timer_running);
    EXPECT_GT(this->progress_timer_reset_count, old_count);

    old_count = this->progress_timer_reset_count;
    ls1.set_term(ls2.term()+1);
    this->audit->handle_leader_status(ls1);
    EXPECT_TRUE(this->progress_timer_running);
    EXPECT_GT(this->progress_timer_reset_count, old_count);

    EXPECT_EQ(this->audit->error_count(), 0u);
}

TEST_F(audit_test, audit_no_error_when_leader_progress)
{
    this->progress_timer_status_expectations();
    this->build_audit();

    EXPECT_FALSE(this->progress_timer_running);

    leader_status ls1;
    ls1.set_leader("joe");

    uint old_count = this->progress_timer_reset_count;
    for(auto i : boost::irange(0, 5))
    {
        ls1.set_current_commit_index(i);
        ls1.set_current_log_index(i+2);

        this->audit->handle_leader_status(ls1);

        EXPECT_TRUE(this->progress_timer_running);
        EXPECT_GT(this->progress_timer_reset_count, old_count);

        old_count = this->progress_timer_reset_count;
    }

    EXPECT_EQ(this->audit->error_count(), 0u);
}

TEST_F(audit_test, audit_throws_error_when_leader_alive_then_stuck)
{
    this->progress_timer_status_expectations();
    this->build_audit();

    leader_status ls1;
    ls1.set_leader("joe");
    ls1.set_current_commit_index(5);
    ls1.set_current_log_index(5);

    leader_status ls2 = ls1;
    ls2.set_current_log_index(17);

    this->audit->handle_leader_status(ls1);
    EXPECT_FALSE(this->progress_timer_running);
    this->audit->handle_leader_status(ls2);
    EXPECT_TRUE(this->progress_timer_running);
    this->leader_progress_timer_callback(boost::system::error_code());

    EXPECT_EQ(this->audit->error_count(), 1u);
}

TEST_F(audit_test, audit_throws_error_when_leader_switch_then_stuck)
{
    this->build_audit();

    leader_status ls1;
    ls1.set_leader("joe");
    ls1.set_current_commit_index(5);
    ls1.set_current_log_index(5);

    leader_status ls2 = ls1;
    ls2.set_leader("freddie");
    ls2.set_term(ls1.term() + 1);

    this->audit->handle_leader_status(ls1);
    this->audit->handle_leader_status(ls2);
    EXPECT_EQ(this->audit->error_count(), 0u);

    this->leader_alive_timer_callback(boost::system::error_code());
    EXPECT_EQ(this->audit->error_count(), 1u);
}

TEST_F(audit_test, audit_forgets_old_data)
{
    this->mem_size = 10;
    this->build_audit();

    leader_status ls1;
    ls1.set_leader("joe");

    leader_status ls2;
    ls2.set_leader("alfred");

    raft_commit_notification com;
    com.set_operation("Do some stuff!!");

    for(auto i : boost::irange(0, 100))
    {
        // Trigger an error, a leader elected, and a commit notification every iteration
        ls1.set_term(i);
        ls2.set_term(i);
        com.set_log_index(i);

        this->audit->handle_leader_status(ls1);
        this->audit->handle_leader_status(ls2);
        this->audit->handle_raft_commit(com);
    }

    // It's allowed to have mem size each of commits, leaders, and errors
    EXPECT_LE(this->audit->current_memory_size(), 3*this->mem_size);
}

TEST_F(audit_test, audit_still_detects_new_errors_after_forgetting_old_data)
{
    this->mem_size = 10;
    this->build_audit();

    leader_status ls1;
    ls1.set_leader("joe");

    raft_commit_notification com;
    com.set_operation("do exciting things and stuff");

    for(auto i : boost::irange(0, 100))
    {
        ls1.set_term(i);
        com.set_log_index(i);

        this->audit->handle_leader_status(ls1);
        this->audit->handle_raft_commit(com);
    }

    EXPECT_EQ(this->audit->error_count(), 0u);

    ls1.set_leader("not joe");
    com.set_operation("don't do anything");

    this->audit->handle_leader_status(ls1);
    this->audit->handle_raft_commit(com);

    EXPECT_EQ(this->audit->error_count(), 2u);
}

TEST_F(audit_test, audit_still_counts_errors_after_forgetting_their_data)
{
    this->mem_size = 10;
    this->build_audit();

    leader_status ls1;
    ls1.set_leader("joe");

    leader_status ls2;
    ls2.set_leader("alfred");

    for(auto i : boost::irange(0, 100))
    {
        // Trigger an error, a leader elected, and a commit notification every iteration
        ls1.set_term(i);
        ls2.set_term(i);

        this->audit->handle_leader_status(ls1);
        this->audit->handle_leader_status(ls2);
    }

    EXPECT_GT(this->audit->error_count(), this->audit->error_strings().size());
    EXPECT_EQ(this->audit->error_strings().size(), this->mem_size);
}

TEST_F(audit_test, audit_sends_monitor_message_when_leader_conflict)
{
    bool error_reported = false;

    EXPECT_CALL(*(this->socket), async_send_to(_,_,_)).WillRepeatedly(Invoke([&](
         const boost::asio::const_buffer& msg,
         boost::asio::ip::udp::endpoint /*ep*/,
         std::function<void(const boost::system::error_code&, size_t)> /*handler*/)
             {
                 std::string msg_string(boost::asio::buffer_cast<const char*>(msg), msg.size());
                 if (msg_string.find(bzn::LEADER_CONFLICT_METRIC_NAME) != std::string::npos)
                 {
                     error_reported = true;
                 }
             }

    ));

    auto ep = boost::asio::ip::udp::endpoint{
          boost::asio::ip::address::from_string("127.0.0.1")
        , 8125
    };
    auto epopt = std::optional<boost::asio::ip::udp::endpoint>{ep};
    this->endpoint = epopt;
    this->build_audit();

    leader_status ls1;
    ls1.set_leader("joe");
    ls1.set_current_commit_index(5);
    ls1.set_current_log_index(5);

    leader_status ls2 = ls1;
    ls2.set_leader("francine");

    this->audit->handle_leader_status(ls1);
    this->audit->handle_leader_status(ls2);

    EXPECT_TRUE(error_reported);
}


TEST_F(audit_test, audit_sends_monitor_message_when_commit_conflict)
{
    bool error_reported = false;

    EXPECT_CALL(*(this->socket), async_send_to(_,_,_)).WillRepeatedly(Invoke([&](
         const boost::asio::const_buffer& msg,
         boost::asio::ip::udp::endpoint /*ep*/,
         std::function<void(const boost::system::error_code&, size_t)> /*handler*/)
             {
                 std::string msg_string(boost::asio::buffer_cast<const char*>(msg), msg.size());
                 if (msg_string.find(bzn::RAFT_COMMIT_CONFLICT_METRIC_NAME) != std::string::npos)
                 {
                     error_reported = true;
                 }
             }

    ));

    auto ep = boost::asio::ip::udp::endpoint{
            boost::asio::ip::address::from_string("127.0.0.1")
            , 8125
    };
    auto epopt = std::optional<boost::asio::ip::udp::endpoint>{ep};
    this->endpoint = epopt;


    this->build_audit();

    raft_commit_notification com1;
    com1.set_log_index(2);
    com1.set_operation("the first thing");

    raft_commit_notification com2 = com1;
    com2.set_operation("the second thing");

    this->audit->handle_raft_commit(com1);
    this->audit->handle_raft_commit(com2);

    EXPECT_TRUE(error_reported);
}
