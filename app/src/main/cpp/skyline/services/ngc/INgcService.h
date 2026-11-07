// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <services/serviceman.h>

namespace skyline::service::ngc {
    /**
     * @brief INgcService or ngc:u is the profanity filter that games use for names and chat messages. There is no word list in the emulator so nothing is ever reported or masked
     * @url https://switchbrew.org/wiki/NGC_services
     */
    class INgcService : public BaseService {
      public:
        INgcService(const DeviceState &state, ServiceManager &manager);

        /**
         * @brief Returns the version of the word list, the same value is always returned
         */
        Result GetContentVersion(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Checks a text for profane words: no flag is set in the output, nothing was found
         */
        Result Check(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Masks the profane words of a text: the text is returned unchanged
         */
        Result Mask(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Reloads the word list, nothing to do
         */
        Result Reload(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        SERVICE_DECL(
            SFUNC(0x0, INgcService, GetContentVersion),
            SFUNC(0x1, INgcService, Check),
            SFUNC(0x2, INgcService, Mask),
            SFUNC(0x3, INgcService, Reload),
            SFUNC(0x4, INgcService, Check),
            SFUNC(0x5, INgcService, Mask)
        )
    };

    /**
     * @brief ITextFilter or ngct:u is the filter for the text of a game, it accepts every text
     */
    class ITextFilter : public BaseService {
      public:
        ITextFilter(const DeviceState &state, ServiceManager &manager);

        /**
         * @brief Tells if a text has a word that has to be filtered: it never does
         */
        Result Match(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns the text as it is
         */
        Result Filter(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        SERVICE_DECL(
            SFUNC(0x0, ITextFilter, Match),
            SFUNC(0x1, ITextFilter, Filter)
        )
    };
}
