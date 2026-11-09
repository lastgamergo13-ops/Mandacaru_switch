// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <mutex>
#include "KSyncObject.h"

namespace skyline::service {
    class BaseService;
}

namespace skyline::kernel::type {
    /**
     * @brief The table of services that a domain session exposes as virtual handles, it is shared between a domain session and all of its clones (like the domain of the real kernel) so an object created through one of them can be used through the others
     */
    class DomainTable {
      private:
        std::mutex mutex;
        std::vector<std::shared_ptr<service::BaseService>> services;

      public:
        /**
         * @return The object ID of the added service
         */
        KHandle Add(const std::shared_ptr<service::BaseService> &service) {
            std::scoped_lock lock{mutex};
            services.push_back(service);
            return static_cast<KHandle>(services.size() - 1);
        }

        /**
         * @note This throws std::out_of_range for an unknown object ID
         */
        std::shared_ptr<service::BaseService> Get(size_t id) {
            std::scoped_lock lock{mutex};
            return services.at(id);
        }

        void Reset(size_t id) {
            std::scoped_lock lock{mutex};
            services.at(id).reset();
        }

        std::vector<std::shared_ptr<service::BaseService>> Snapshot() {
            std::scoped_lock lock{mutex};
            return services;
        }
    };

    /**
     * @brief KService holds a reference to a service, this is equivalent to KClientSession
     */
    class KSession : public KSyncObject {
      public:
        std::shared_ptr<service::BaseService> serviceObject;
        std::shared_ptr<DomainTable> domainTable{std::make_shared<DomainTable>()}; //!< The services that correspond to virtual handles, shared with the clones of this session
        bool isOpen{true}; //!< If the session is open or not
        bool isDomain{}; //!< If this is a domain session or not

        /**
         * @param serviceObject A shared pointer to the service class
         */
        KSession(const DeviceState &state, std::shared_ptr<service::BaseService> &serviceObject) : serviceObject(serviceObject), KSyncObject(state, KType::KSession) {}

        /**
         * @brief Converts this session into a domain session
         * @url https://switchbrew.org/wiki/IPC_Marshalling#Domains
         * @return The virtual handle of this service in the domain
         */
        KHandle ConvertDomain() {
            isDomain = true;
            return domainTable->Add(serviceObject);
        }
    };
}
