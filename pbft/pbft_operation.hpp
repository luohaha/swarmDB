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

#pragma once

#include <include/bluzelle.hpp>
#include <proto/bluzelle.pb.h>
#include <bootstrap/bootstrap_peers_base.hpp>
#include <cstdint>
#include <string>
#include <node/session_base.hpp>

namespace bzn
{
    using hash_t = std::string;
    // View, sequence
    using operation_key_t = std::tuple<uint64_t, uint64_t, hash_t>; // view #, seq#, hash

    // View, sequence
    using log_key_t = std::tuple<uint64_t, uint64_t>;

    enum class pbft_operation_state
    {
        prepare, commit, committed
    };


    class pbft_operation
    {
    public:

        pbft_operation(uint64_t view, uint64_t sequence, pbft_request msg, std::shared_ptr<const std::vector<peer_address_t>> peers);

        void set_session(std::weak_ptr<bzn::session_base>);

        static hash_t request_hash(const pbft_request& req);

        operation_key_t get_operation_key();
        pbft_operation_state get_state();

        void record_preprepare(const wrapped_bzn_msg& encoded_preprepare);
        bool has_preprepare();

        void record_prepare(const wrapped_bzn_msg& encoded_prepare);
        bool is_prepared();

        void record_commit(const wrapped_bzn_msg& encoded_commit);
        bool is_committed();

        void begin_commit_phase();
        void end_commit_phase();

        std::weak_ptr<bzn::session_base> session();

        const uint64_t view;
        const uint64_t sequence;
        const pbft_request request;

        std::string debug_string();

        size_t faulty_nodes_bound() const;

        const std::string& get_preprepare() const { return this->preprepare_message; };

        const std::set<std::string> get_prepares() const
        {
            std::set<std::string> prepares;
            for (const auto& message : this->prepare_messages)
            {
                prepares.insert(message.second);
            }
            return prepares;
        };

    private:
        const std::shared_ptr<const std::vector<peer_address_t>> peers;

        pbft_operation_state state = pbft_operation_state::prepare;

        bool preprepare_seen = false;
        std::set<bzn::uuid_t> prepares_seen;
        std::set<bzn::uuid_t> commits_seen;

        std::weak_ptr<bzn::session_base> listener_session;

        std::string preprepare_message;
        std::map<uuid_t, std::string> prepare_messages;  // uuid_t is the sender uuid, prepared messages
    };
}
