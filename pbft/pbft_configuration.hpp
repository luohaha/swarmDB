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
#include <pbft/pbft_base.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace bzn
{
    using hash_t = std::string;

    class pbft_configuration
    {
    public:
        using shared_const_ptr = std::shared_ptr<const pbft_configuration>;

        pbft_configuration();
        bool operator==(const pbft_configuration& other) const;
        bool operator!=(const pbft_configuration& other) const;

        // de-serialize from string - returns true for success
        bool from_string(const std::string& str);

        // serialize to string
        std::string to_string() const;

        // returns the hash of this configuration
        hash_t get_hash() const;

        // returns a sorted vector of peers
        std::shared_ptr<const std::vector<bzn::peer_address_t>> get_peers() const;

        // add a new peer - returns true if success, false if invalid or duplicate peer
        bool add_peer(const bzn::peer_address_t& peer);

        // removes an existing peer - returns true if found and removed
        bool remove_peer(const bzn::peer_address_t& peer);

        // computes differences between this config and another
        // returns pair of vectors of added and removed peers
        std::pair<std::shared_ptr<std::vector<bzn::peer_address_t>>, std::shared_ptr<std::vector<bzn::peer_address_t>>>
            diff(const pbft_configuration& other);

    private:
        void cache_sorted_peers();
        bool conflicting_peer_exists(const bzn::peer_address_t &peer) const;
        bool valid_peer(const bzn::peer_address_t &peer) const;
        bool insert_peer(const bzn::peer_address_t& peer);
        hash_t create_hash() const;

        std::unordered_set<bzn::peer_address_t> peers;
        std::shared_ptr<std::vector<bzn::peer_address_t>> sorted_peers;
        hash_t cached_hash;
    };
}

