#include "Matlab.h"

#include <list>
#include "Process.h"
#include "connection/config/Config.h"

#include "log.h"

namespace Gadgetron::Server::Connection::Nodes {

    Gadgetron::Process::child start_matlab_module(
        const Config::Execute &execute,
        unsigned short port,
        const Gadgetron::Core::StreamContext &context
    ) {

        std::vector<std::pair<std::string, std::string>> env_overrides{
            {"GADGETRON_EXTERNAL_PORT", std::to_string(port)},
            {"GADGETRON_EXTERNAL_MODULE", execute.name},
            {"GADGETRON_STORAGE_ADDRESS", context.storage_address}
        };

        auto module = Process::child(
                Process::search_path("matlab"),
                std::vector<std::string>{"-batch", "gadgetron.external.main"},
                env_overrides
        );

        GINFO_STREAM("Started external MATLAB module (pid: " << module.id() << ").");

        return std::move(module);
    }

    bool matlab_available() noexcept {
        try {
            return std::system("\"matlab\" -batch gadgetron.external.test_available >/dev/null 2>&1") == 0;
        }
        catch (...) {
            return false;
        }
    }
}
