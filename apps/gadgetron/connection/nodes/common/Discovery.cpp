#include "Discovery.h"

#include "ProcessHelper.h"

#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/include/io.hpp>

#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_object.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/qi.hpp>

#include <string>
#include "ProcessHelper.h"

BOOST_FUSION_ADAPT_STRUCT(
    Gadgetron::Server::Connection::Nodes::Remote,
    (std::string, address)(std::string, port)
)

namespace {
    using namespace Gadgetron::Server;
    using namespace Gadgetron::Server::Connection::Nodes;

    std::vector<Address> parse_remote_workers(std::string input)
    {
        namespace qi = boost::spirit::qi;
        namespace ascii = boost::spirit::ascii;
        using ascii::space;
        using qi::_1;
        using qi::char_;
        using qi::double_;
        using qi::lexeme;
        using qi::phrase_parse;

        auto first = input.begin(), last = input.end();

        qi::rule<decltype(first), std::string(), ascii::space_type> ipv6 = '[' >> lexeme[+(char_ - ']')] >> ']';
        qi::rule<decltype(first), Remote(), ascii::space_type> address_rule =
                '"' >>
                (lexeme[+(char_ - (lexeme[':'] | lexeme[']'] | lexeme['[']))] | ipv6) >>
                ':' >>
                (lexeme[+(char_ - '"')]) >>
                '"';

        auto result = std::vector<Address>{};
        bool r = phrase_parse(
                first,
                last,
                ( '[' >> address_rule % ',' >> ']' ),
                space,
                result
        );

        if (first != last || !r ) {
            GWARN_STREAM("Failed to parse worker list from discovery command: " << input);
            return std::vector<Address>{};
        }

        return result;
    }
}

namespace Gadgetron::Server::Connection::Nodes {

    std::vector<Address> discover_remote_peers()
    {
        auto worker_discovery_command = std::getenv("GADGETRON_REMOTE_WORKER_COMMAND");
        if (!worker_discovery_command) return std::vector<Address>{};

        GDEBUG_STREAM("Worker discovery command: " << worker_discovery_command);

        std::string output = Process::capture_output(worker_discovery_command);
        if (output.empty()) {
            GWARN_STREAM("No output from remote worker command");
            return std::vector<Address>();
        }

        return parse_remote_workers(output);
    }

    std::vector<Address> discover_peers() {

        auto workers = discover_remote_peers();

        if (workers.empty()) {
            GWARN_STREAM(
                    "Remote worker list empty; adding local worker. " <<
                    "This machine will perform reconstructions. " <<
                    "This is probably not what you intended."
            )
            workers.emplace_back(Local{});
        }

        return workers;
    }
}
