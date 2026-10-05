#include "Python.h"

#include <list>
#include "ProcessHelper.h"

#include "connection/config/Config.h"

#include <boost/asio.hpp>

#include "log.h"
#include <regex>

namespace Gadgetron::Server::Connection::Nodes {

    using namespace Gadgetron::Core;


    Gadgetron::Process::child start_julia_module(
        const Config::Execute &execute,
        unsigned short port,
        const StreamContext &context
    ) {
        if (!execute.target) throw std::invalid_argument("Target must be specified for Julia modules");

        std::list<std::string> args{
            "-e", "import Gadgetron; Gadgetron.External.main()",
            std::to_string(port),
            execute.name, *execute.target
        };
        std::vector<std::pair<std::string, std::string>> env_overrides{
            {"GADGETRON_STORAGE_ADDRESS", context.storage_address}
        };

        auto module = Process::child(
                Process::search_path("julia"),
                std::vector<std::string>(args.begin(), args.end()),
                env_overrides
        );

        GINFO_STREAM("Started external Julia module (pid: " << module.id() << ").");
        return std::move(module);
    }

    bool julia_available() noexcept {
        try {
            return std::system("\"julia\" -e 'using Gadgetron' >/dev/null 2>&1") == 0;
        }
        catch (...) {
            return false;
        }
    }
}
