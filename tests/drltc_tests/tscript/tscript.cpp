
#include <algorithm>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <fc/filesystem.hpp>
#include <fc/optional.hpp>
#include <fc/string.hpp>
#include <fc/variant.hpp>
#include <fc/variant_object.hpp>

#include <fc/io/buffered_iostream.hpp>
#include <fc/io/iostream.hpp>
#include <fc/io/fstream.hpp>
#include <fc/io/json.hpp>
#include <fc/io/sstream.hpp>
#include <fc/rpc/json_connection.hpp>

#include <bts/blockchain/genesis_config.hpp>
#include <bts/cli/cli.hpp>
#include <bts/cli/print_result.hpp>
#include <bts/client/client.hpp>
#include <bts/net/node.hpp>
#include <bts/rpc/rpc_server.hpp>

namespace bts { namespace tscript {

class client_context
{
public:
    client_context();
    virtual ~client_context();
    
    void configure_client_from_args();
    void start();
    
    std::vector<std::string> args;
    bts::client::client_ptr client;
    fc::future<void> client_done;
    std::map<std::string, std::string> template_context;
};

typedef std::shared_ptr<client_context> client_context_ptr;

class context
{
public:
    context();
    virtual ~context();

    void set_genesis(const fc::path& genesis_json_filename);

    void create_client(const std::string& name);
    fc::path get_data_dir_for_client(const std::string& name) const;
    
    void get_clients_by_variant(const fc::variant& spec, std::vector<client_context_ptr>& result, int depth=0);
    
    fc::path basedir;
    std::vector<client_context_ptr> v_clients;
    std::map<std::string, client_context_ptr> m_name_client;
    std::map<std::string, std::vector<std::string>> m_name_client_group;
    std::map<std::string, std::string> template_context;
    fc::path genesis_json_filename;
    bts::blockchain::genesis_block_config genesis_config;
    bts::net::simulated_network_ptr sim_network;
    std::string current_client_name;
};

typedef std::shared_ptr<context> context_ptr;

class interpreter
{
public:
    interpreter();
    virtual ~interpreter();
    
    void run_single_command(const fc::variants& cmd);
    void run_command_list(const fc::variants& cmd_list);
    void run_file(const fc::path& path);
    void run_cli_command(
        const std::string& client_name,
        const std::string& action
        );
    void run_interp_command(
        const fc::variants& cmd
        );
    
    void cmd_clients(const fc::variants& args);
    void cmd_x(const fc::variants& cmd);
    
    void interact();
    
    context_ptr ctx;
    bts::cli::print_result printer;
};

inline bool startswith(
    const std::string& a,
    const std::string& prefix
    )
{
    size_t m = prefix.length(), n = a.length();
    return (m <= n) && (0 == a.compare( 0, m, prefix ));
}

inline bool endswith(
    const std::string& a,
    const std::string& suffix
    )
{
    size_t m = suffix.length(), n = a.length();
    return (m <= n) && (0 == a.compare( n-m, m, suffix ));
}

inline bool split1(
    const std::string& s,
    const std::string& divider,
    std::string& first,
    std::string& second
    )
{
    size_t p = s.find( divider );
    if( p == std::string::npos )
    {
        first = s;
        return false;
    }
    first = s.substr( 0, p );
    second = s.substr( p+1 );
    return true;
}

inline void copy_str_to_buffer_with_0(std::vector<char> &buf, const std::string& s)
{
    std::copy(s.c_str(), s.c_str()+s.length(), std::back_inserter(buf));
    buf.push_back('\0');
    return;
}

fc::variants split_line( const std::string& line )
{
    try
    {
        // Use up-to-date parser
        fc::istream_ptr ss_line = std::make_shared<fc::stringstream>( line );
        fc::buffered_istream bi_line( ss_line );
        fc::variants result;

        while( true )
        {
            try
            {
                fc::variant v = fc::json::from_stream( bi_line, fc::json::relaxed_parser );
                result.push_back( v );
            }
            catch( const fc::parse_error_exception& e )
            {
                std::cout << "split_line() caught exception:\n";
                std::cout << e.to_detail_string() << "\n";
                break;
            }
            catch( const fc::eof_exception& e )
            {
                break;
            }
        }
        return result;
    }
    FC_CAPTURE_AND_RETHROW()
}

client_context::client_context()
{
    return;
}

client_context::~client_context()
{
    return;
}

void client_context::configure_client_from_args()
{
    try
    {
        std::vector<char> buf;
        std::vector<size_t> arg_offset;
        
        std::string arg_0 = "bitshares_client";
        arg_offset.push_back(0);
        copy_str_to_buffer_with_0(buf, arg_0);
        
        int argc = (int) this->args.size() + 1;
        
        for(int i=1;i<argc;i++)
        {
            arg_offset.push_back(buf.size());
            copy_str_to_buffer_with_0(buf, this->args[i-1]);
        }

        std::vector<char*> v_argv;
        v_argv.push_back(buf.data());
        for(int i=1;i<argc;i++)
            v_argv.push_back(v_argv[0] + arg_offset[i]);

        std::cout << "client invocation:";
        for(int i=0;i<argc;i++)
        {
            std::cout << v_argv.data()[i] << " ";
        }
        std::cout << "\n";

        this->client->configure_from_command_line(argc, v_argv.data());
    }
    FC_CAPTURE_AND_RETHROW();

    return;
}

void client_context::start()
{
    try
    {
        this->client_done = this->client->start();
    }
    FC_CAPTURE_AND_RETHROW()
    return;
}

context::context()
{
    try
    {
        this->sim_network = std::make_shared<bts::net::simulated_network>("tscript");
        this->m_name_client_group["all"] = std::vector<std::string>();
        this->basedir = "tmp";
    }
    FC_CAPTURE_AND_RETHROW()
    return;
}

context::~context()
{
    return;
}

void context::set_genesis(const fc::path& genesis_json_filename)
{
    try
    {
        this->genesis_json_filename = genesis_json_filename;
        this->genesis_config = fc::json::from_file(genesis_json_filename).as<bts::blockchain::genesis_block_config>();
        this->template_context["genesis.timestamp"] = this->genesis_config.timestamp.to_iso_string();
    }
    FC_CAPTURE_AND_RETHROW()
    return;
}

void context::create_client(const std::string& name)
{
    try
    {
        client_context_ptr cc = std::make_shared<client_context>();
        cc->args.push_back("--disable-default-peers");
        cc->args.push_back("--log-commands");
        cc->args.push_back("--ulog=0");
        cc->args.push_back("--min-delegate-connection-count=0");
        cc->args.push_back("--upnp=false");
        cc->args.push_back("--genesis-config=" + this->genesis_json_filename.string());
        cc->args.push_back("--data-dir=" + this->get_data_dir_for_client(name).string());
        
        cc->client = std::make_shared<bts::client::client>("tscript", this->sim_network);
        cc->configure_client_from_args();
        cc->client->set_client_debug_name(name);
        cc->template_context["client.name"] = name;

        this->m_name_client[name] = cc;
        this->m_name_client_group["all"].push_back(name);
    }
    FC_CAPTURE_AND_RETHROW()

    return;
}

fc::path context::get_data_dir_for_client(const std::string& name) const
{
    try
    {
        return this->basedir / "client" / name;
    }
    FC_CAPTURE_AND_RETHROW()
}

void context::get_clients_by_variant(const fc::variant& spec, std::vector<client_context_ptr>& result, int depth)
{
    try
    {
        FC_ASSERT(depth < 20);
        
        if( spec.is_array() )
        {
            const fc::variants& v = spec.get_array();
            for( const fc::variant& e : v )
                this->get_clients_by_variant(e, result, depth+1);
            return;
        }

        if( spec.is_string() )
        {
            std::string target_name = spec.get_string();
            std::cout << "in spec.is_string() : target=" << target_name << "\n";
            auto p = this->m_name_client_group.find(target_name);
            if( p != this->m_name_client_group.end() )
            {
                std::cout << "   found group\n";
                for( const fc::string& e : p->second )
                {
                    auto q = this->m_name_client.find(e);
                    if( q != this->m_name_client.end() )
                    {
                        result.push_back(q->second);
                        std::cout << "   group member " << e << " found\n";
                    }
                    else
                        FC_ASSERT(false, "couldn't find named client when expanding group definition");
                }
                return;
            }
            auto q = this->m_name_client.find(target_name);
            if( q != this->m_name_client.end() )
            {
                std::cout << "   found singleton\n";
                result.push_back(q->second);
                return;
            }
            FC_ASSERT(false, "couldn't find named client");
        }
        
        FC_ASSERT(false, "expected: client-spec");
    }
    FC_CAPTURE_AND_RETHROW()
}

interpreter::interpreter()
{
    try
    {
        this->ctx = std::make_shared<context>();
    }
    FC_CAPTURE_AND_RETHROW()
    return;
}

interpreter::~interpreter()
{
    return;
}

void interpreter::run_single_command(const fc::variants& cmd)
{
    try
    {
        if( cmd.size() == 0 )
            return;
        
        // parse the command
        std::string action = cmd[0].get_string();
        
        if( action == "x")
        {
            this->cmd_x(cmd);
        }
        else if( (action.length() > 0) && (action[0] == '!') )
        {
            this->run_interp_command( cmd );
        }
        else
        {
            FC_ASSERT(false, "unknown command");
        }
    }
    FC_CAPTURE_AND_RETHROW()
    return;
}

void interpreter::run_interp_command( const fc::variants& cmd )
{
    try
    {
        std::string action = cmd[0].get_string();

        if( action == "!clients" )
        {
            this->cmd_clients( cmd );
        }
        else if( action == "!defgroup" )
        {
            FC_ASSERT(false, "unimplemented command");
        }
        else if( action == "!include" )
        {
            FC_ASSERT(false, "unimplemented command");
        }
        else if( action == "!pragma" )
        {
            FC_ASSERT(false, "unimplemented command");
        }
        else
        {
            FC_ASSERT(false, "unknown command");
        }
    }
    FC_CAPTURE_AND_RETHROW()

    return;
}

void interpreter::run_cli_command(
    const std::string& client_name,
    const std::string& action
    )
{
    try
    {
        // commands beginning with [ are JSON.
        // commands beginning with ! are preprocess.
        // commands beginning with a letter are CLI.
        if( action[0] == '[' )
        {
            fc::variants cmd = fc::json::from_string( action ).as<fc::variants>();
            this->run_single_command( cmd );
        }
        else
        {
            // split command according to CLI command splitting logic
            std::cout << "splitting line: " << action << "\n";
            fc::variants rpc_cmd_args = split_line( action );
            if( rpc_cmd_args.size() == 0 )
                return;
            
            fc::variant rpc_cmd = rpc_cmd_args[0];
            rpc_cmd_args.erase( rpc_cmd_args.begin() );
            
            fc::variants actual_cmd( { "x", client_name, rpc_cmd, rpc_cmd_args } );
            std::cout << "line split as: " << fc::json::to_string( rpc_cmd ) << "\n";
            std::cout << "running as: " << fc::json::to_string( actual_cmd ) << "\n";
            this->run_single_command( actual_cmd );
        }
    }
    FC_CAPTURE_AND_RETHROW()

    return;
}

void interpreter::run_command_list(const fc::variants& cmd_list)
{
    for(const fc::variant& cmd : cmd_list)
    {
        try
        {
            this->run_single_command(cmd.as<fc::variants>());
        }
        FC_CAPTURE_AND_RETHROW( (cmd) );
    }
    return;
}

void interpreter::run_file(const fc::path& path)
{
    fc::variants v;
    try
    {
        v = fc::json::from_file<fc::variants>(path);
    }
    catch( const fc::exception& e )
    {
        std::cout << "problem parsing file " << path.string() << "\n";
        std::cout << e.to_detail_string() << "\n";
        exit(1);
    }
    this->run_command_list(v);
    return;
}

void interpreter::cmd_clients(const fc::variants& cmd)
{
    try
    {
        FC_ASSERT(cmd.size() >= 2);
        
        fc::variants args = cmd[1].get_array();
        
        // create clients
        for( const fc::variant& e : args )
        {
            std::string cname = e.as<std::string>();
            std::cout << "creating client " << cname << "\n";
            this->ctx->create_client(cname);
        }
    }
    FC_CAPTURE_AND_RETHROW()
    return;
}

void interpreter::cmd_x(const fc::variants& cmd)
{
    try
    {
        FC_ASSERT( cmd.size() >= 3 );

        std::cout << "in cmd_x\n";
        
        std::vector<bts::tscript::client_context_ptr> targets;
        
        this->ctx->get_clients_by_variant( cmd[1], targets );

        std::cout << "targets found: " << targets.size() << "\n";

        for( bts::tscript::client_context_ptr& t : targets )
        {
            std::cout << "   " << t->client->debug_get_client_name() << ":\n";

            const std::string& method_name = cmd[2].get_string();

            std::cout << "      " << method_name << "\n";
            
            // create context for command by combining template context dictionaries
            fc::mutable_variant_object effective_ctx;
            for( auto& e : this->ctx->template_context )
                effective_ctx[e.first] = e.second;
            for( auto& e : t->template_context )
                effective_ctx[e.first] = e.second;
            fc::variant_object v_effective_ctx = effective_ctx;
            
            // substitute into parameters
            fc::variants args;
            if( cmd.size() >= 4 )
                args = cmd[3].get_array();
            
            for( size_t i=0,n=args.size(); i<n; i++ )
            {
                if( args[i].is_string() )
                {
                    args[i] = fc::format_string(args[i].get_string(), v_effective_ctx);
                }
                std::cout << "      " << fc::json::to_string(args[i]) << "\n";
            }
            
            fc::optional<fc::variant> result;
            fc::optional<fc::exception> exc;
            
            std::cout << "{\n";
            try
            {
                result = t->client->get_rpc_server()->direct_invoke_method(method_name, args);
                fc::string json_result = fc::json::to_string(result);
                std::stringstream ss_print;
                this->printer.format_and_print( method_name, args, result, t->client.get(), ss_print );
                std::string printed_result = ss_print.str();
                // TODO:  Escape printed_result
                std::cout << "  result : " << json_result << ",\n"
                             "  printed_result : \"\"\"\n" << printed_result << "\n\"\"\",\n"
                             "  exception : null\n";
            }
            catch( const fc::exception& e )
            {
                exc = e;
                std::cout << "  result : null,\n"
                             "  printed_result : \"\"\n"
                             "  exception : {\n"
                             "    name : \"" << e.name() << "\",\n"
                             "    code : " << e.code() << ",\n"
                             "    message : \"" << e.to_string() << "\","
                             "    detail : \"\"\"\n" << e.to_detail_string() << "\n\"\"\""
                             "  }\n";
                // TODO:  Escape above 
            }
            std::cout << "}\n";

            std::string cmp_op = "nop";
            fc::variant cmp_op_arg;
            if( cmd.size() >= 5 )
            {
                cmp_op = cmd[4].get_string();
                if( cmd.size() >= 6 )
                {
                    cmp_op_arg = cmd[5];
                }
            }
            
            bool expected_exception = false;
            
            if( cmp_op == "nop" )
                ;
            else if( cmp_op == "eq" )
            {
                FC_ASSERT(result.valid());
                std::cout << "*result : " << fc::json::to_string(*result) << "\n";
                std::cout << "cmp_op_arg : " << fc::json::to_string(cmp_op_arg) << "\n";
                FC_ASSERT(((*result) == cmp_op_arg).as_bool());
            }
            else if( cmp_op == "ex" )
            {
                expected_exception = true;
            }
            else if( cmp_op == "ss" )
            {
                FC_ASSERT(result.valid());
                FC_ASSERT(cmp_op_arg.is_object());
                FC_ASSERT(result->is_object());
                
                fc::variant_object vo_result = result->get_object();
                for( auto& kv : cmp_op_arg.get_object() )
                {
                    auto it = vo_result.find(kv.key());
                    FC_ASSERT( it != vo_result.end() );
                    FC_ASSERT( (it->value() == kv.value()).as_bool() );
                }
            }
            
            if( (!expected_exception) && (!result.valid()) )
                FC_ASSERT(false, "terminating due to unexpected exception");
        }
        std::cout << "\n";
    }
    FC_CAPTURE_AND_RETHROW()

    return;
}

void interpreter::interact()
{
    while( true )
    {
        try
        {
            std::string prompt = this->ctx->current_client_name + " >>> ";
            std::string line = bts::cli::get_line( &std::cin, &std::cout, prompt );
            
            std::string client_name, action;
            
            if( split1( line, ":", client_name, action ) )
            {
                if( action == "" )
                {
                    // client:
                    this->ctx->current_client_name = line.substr( 0, line.length()-1 );
                    continue;
                }
                // client:action
                this->run_cli_command( client_name, action );
            }
            else
            {
                // action
                this->run_cli_command( this->ctx->current_client_name, line );
            }
        }
        catch( fc::exception& e )
        {
            std::cout << "caught exception:\n" << e.to_detail_string() << "\n";
        }
    }
    return;
}

void run_single_tscript(
    const fc::path& genesis_json_filename,
    const fc::path& tscript_filename,
    bool break_after
    )
{
    try
    {
        FC_ASSERT( fc::is_regular_file(tscript_filename) );

        // delete client directories
        fc::remove_all( "tmp/client" );

        bts::tscript::interpreter interp;
        interp.ctx->set_genesis(genesis_json_filename);
        try
        {
            interp.run_file(tscript_filename);
        }
        catch( const fc::exception& e )
        {
            std::cout << e.to_detail_string() << "\n";
        }
        
        if( break_after )
            interp.interact();
    }
    FC_CAPTURE_AND_RETHROW()

    return;
}

void run_tscripts(
    const fc::path& genesis_json_filename,
    const fc::path& target_filename,
    bool break_after
    )
{
    try
    {
        if( !fc::is_directory(target_filename) )
        {
            run_single_tscript( genesis_json_filename, target_filename, break_after );
            return;
        }

        for( fc::recursive_directory_iterator it(target_filename), it_end;
             it != it_end; ++it )
        {
            fc::path filename = (*it);
            try
            {
                if( !fc::is_regular_file(filename) )
                    continue;
                if( !endswith(filename.string(), ".tscript") )
                    continue;
                run_single_tscript(genesis_json_filename, filename, break_after);
            }
            FC_CAPTURE_AND_RETHROW( (filename) );
        }
    }
    FC_CAPTURE_AND_RETHROW( (target_filename) );
    return;
}

} }

void help(int argc, char** argv)
{
    fc::mutable_variant_object d;
    
    d["program_name"] = argv[0];
    
    std::cout << fc::format_string(R"msg(Usage:

    ${program_name} [options] targets
    
Run on one or more *targets*.  Each target may be a .tscript file,
or a directory containing .tscript files.

Options:

    --break : Break after executing each tscript.
    --nobreak : Don't break after executing each tscript.
    --help : Display this help message.

)msg", d);
    return;
}

int main(int argc, char** argv, char** envp)
{
    std::vector<std::string> s_argv;
    
    for( int i=1; i<argc; i++ )
        s_argv.push_back(std::string(argv[i]));

    for( const std::string& arg : s_argv )
    {
        std::string larg = fc::to_lower( arg );
        if(   (larg == "--help")
           || (larg == "-h")
           || (larg == "-?")
           || (larg == "/h")
           || (larg == "/help")
          )
        {
            help(argc, argv);
            return 0;
        }
    }
    
    bool break_after = false;
    
    for( int i=1; i<argc; i++ )
    {
        const std::string& arg = s_argv[i-1];
        std::string larg = fc::to_lower( arg );
        if( larg == "--break" )
        {
            // break into CLI of first client
            break_after = true;
        }
        else
        {
            bts::tscript::run_tscripts( "tmp/genesis.json", arg, break_after );
        }
    }
    return 0;
}
