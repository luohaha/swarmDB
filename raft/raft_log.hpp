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
#include <raft/log_entry.hpp>
#include <vector>
#include <string>
#include <fstream>


namespace bzn {

    const std::string MSG_ERROR_EMPTY_LOG_ENTRY_FILE{"Empty log entry file. Please delete .state folder."};
    const std::string MSG_NO_PEERS_IN_LOG{"Unable to find peers in log entries."};
    const std::string MSG_TRYING_TO_INSERT_INVALID_ENTRY{"Trying to insert an invalid log entry."};
    const std::string MSG_UNABLE_TO_CREATE_LOG_PATH_NAMED{"Unable to create log path: "};
    const std::string MSG_EXITING_DUE_TO_LOG_PATH_CREATION_FAILURE{"MSG_EXITING_DUE_TO_LOG_PATH_CREATION_FAILURE"};
    const std::string MSG_ERROR_MAXIMUM_STORAGE_EXCEEDED{"Maximum storage has been exceeded, please update the options file."};
    const size_t DEFAULT_MAX_STORAGE_SIZE{2147483648}; // The default maximum allowed storage for a node is 2G

    class raft_log
    {
    public:
        raft_log(const std::string& log_path, const size_t max_storage = bzn::DEFAULT_MAX_STORAGE_SIZE);

        ~raft_log();

        const bzn::log_entry& entry_at(size_t i) const;
        const bzn::log_entry& last_quorum_entry() const;

        void leader_append_entry(const bzn::log_entry& log_entry);
        void follower_insert_entry(size_t index, const bzn::log_entry& log_entry);

        bool entry_accepted(size_t previous_index, size_t previous_term) const;

        size_t size() const;

        inline size_t memory_used() const {return this->total_memory_used;};

        inline const std::vector<log_entry>& get_log_entries()
        {
            return this->log_entries;
        }


        inline bool maximum_storage_exceeded() const
        {
            return this->maximum_storage < this->total_memory_used;
        }

    private:
        void append_log_disk(const bzn::log_entry& log_entry);
        void rewrite_log();

        std::vector<log_entry> log_entries;
        size_t                  total_memory_used = 0;
        const size_t            maximum_storage;

        std::ofstream log_entry_out_stream;
        const std::string entries_log_path;
    };
}
