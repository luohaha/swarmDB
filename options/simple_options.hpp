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

#include <boost/program_options.hpp>
#include <string>

namespace bzn::option_names
{
    const std::string AUDIT_MEM_SIZE = "audit_mem_size";
    const std::string AUDIT_ENABLED = "audit_enabled";
    const std::string BOOTSTRAP_PEERS_FILE = "bootstrap_file";
    const std::string BOOTSTRAP_PEERS_URL = "bootstrap_url";
    const std::string DEBUG_LOGGING = "debug_logging";
    const std::string ETHERERUM_IO_API_TOKEN = "ethereum_io_api_token";
    const std::string ETHERERUM = "ethereum";
    const std::string HTTP_PORT = "http_port";
    const std::string LISTENER_ADDRESS = "listener_address";
    const std::string LISTENER_PORT = "listener_port";
    const std::string LOG_TO_STDOUT = "log_to_stdout";
    const std::string LOGFILE_DIR = "logfile_dir";
    const std::string LOGFILE_MAX_SIZE = "logfile_max_size";
    const std::string LOGFILE_ROTATION_SIZE = "logfile_rotation_size";
    const std::string MAX_STORAGE = "max_storage";
    const std::string MONITOR_ADDRESS = "monitor_address";
    const std::string MONITOR_PORT = "monitor_port";
    const std::string NODE_UUID = "uuid";
    const std::string NODE_PUBKEY_FILE = "public_key_file";
    const std::string NODE_PRIVATEKEY_FILE = "private_key_file";
    const std::string PBFT_ENABLED = "use_pbft";
    const std::string STATE_DIR = "state_dir";
    const std::string WS_IDLE_TIMEOUT = "ws_idle_timeout";
    const std::string PEER_VALIDATION_ENABLED = "peer_validation_enabled";

    const std::string CHAOS_ENABLED = "chaos_testing_enabled";
    const std::string CHAOS_NODE_FAILURE_SHAPE = "chaos_node_failure_shape";
    const std::string CHAOS_NODE_FAILURE_SCALE = "chaos_node_failure_scale_hours";
    const std::string CHAOS_MESSAGE_DROP_CHANCE = "chaos_message_drop_chance";
    const std::string CHAOS_MESSAGE_DELAY_CHANCE = "chaos_message_delay_chance";
    const std::string CHAOS_MESSAGE_DELAY_TIME = "chaos_message_delay_time_milliseconds";


}

namespace bzn
{
    class simple_options
    {
    public:
        simple_options();

        /*
         * Read command line to find location of config file, read config file for everything else
         */
        bool parse(int argc, const char* argv[]);

        /*
         * Get the value of a particular option (options defined in bzn::options) with a particular type
         */
        template<typename T>
        T
        get(const std::string& option_name) const
        {
            if (this->has(option_name))
            {
                return this->vm[option_name].as<T>();
            }
            else
            {
                // T is a string and option_name doesn't exist, the lookup will throw an exception
                return T();
            }
        }

        /*
         * Assign a value to an option at runtime
         */
        void set(const std::string& option_name, const std::string& option_value);

        /*
         * Do we have a value for an option (either explicit or default)
         */
        bool has(const std::string& option_name) const;

    private:
        void build_options();
        bool validate_options();
        bool handle_command_line_options(int argc, const char* argv[]);
        bool handle_config_file_options();

        std::string config_file;
        boost::program_options::options_description options_root;
        boost::program_options::variables_map vm;
    };
}
