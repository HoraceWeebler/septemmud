#include "script_entities/script_entity.h"
#include "entity_manager.h"
#include <memory>
#include "global_settings.h"
#include "config.h"
#include "string_utils.h"
#include "config.h"
#include "spdlog/spdlog.h"
#include <spdlog/sinks/stdout_sinks.h>

namespace spd = spdlog;

script_entity::script_entity(sol::this_state ts, sol::this_environment te, EntityType myType, std::string name)
    : m_type(myType)
    , environment_(NULL)
    , name(name)
{
    lua_State* L = ts;
    
        if (!te) {
                std::cout << "function does not have an environment: exiting function early" << std::endl;
                return;
        }
    sol::environment& env = te;      
    sol::optional<std::string> sp = env["_INTERNAL_SCRIPT_PATH_"];
    
    assert( sp );
    this->SetVirtualScriptPath( sp.value() );
    
    sol::optional<std::string> spv = env["_INTERNAL_PHYSICAL_SCRIPT_PATH_"];
    
    assert( spv );
    this->SetPhysicalScriptPath( spv.value() );
    
   // sol::optional<std::string> sp2 = env["_INTERNAL_SCRIPT_PATH_BASE_"];
    //assert( sp2 );
   // if( sp2 )
   //     this->SetBaseScriptPath( sp2.value() );
    // references the object that called this function
    // in constructors:

    m_userdata = std::shared_ptr<_sol_userdata_>(new _sol_userdata_);
    m_userdata->selfobj = sol::userdata(L, -2);

    // the -1 (NEGATIVE one) above
    // means "off the top fo the stack"
    // (-1 is the top, -2 is one below, etc...)

    // definitely the same
    //    script_entity& self = selfobj.as<script_entity>();
    // assert(&self == this);
    entity_manager::Instance().register_entity(this, sp.value(), myType);
}

script_entity::~script_entity()
{

    if ( virtual_script_path.size() > 0 )
    {
        auto log = spd::get("main");
        std::stringstream ss;
        ss << "Destroyed object, script path= " << virtual_script_path;
        log->info(ss.str());
    }
    
    entity_manager::Instance().deregister_entity(this, m_type);
}
/*
void script_entity::debug(const std::string& msg)
{
    //lua_State* L = ts;
    //sol::userdata selfobj = sol::userdata(L, 1);
    //script_entity& self = selfobj.as<script_entity>();
   // entity_manager::Instance().debug_script(&self, msg);

    // if( !m_log )
    //{

    //                   global_settings::Instance().GetSetting(DEFAULT_ROOM_LOG_PATH);
    // m_log.reset(new entity_log( entityLog, m_type ));
   // auto rotating_logger = spd::rotating_logger_mt(s, entityLog, 1048576 * 5, 3);
   
    auto log = spd::get(strip_path_copy(script_path));//>info("loggers can be retrieved from a global registry using the spdlog::get(logger_name) function");
  
   
    if( !log )
    {
        
        std::string entityLog = global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH) +
                            global_settings::Instance().GetSetting(DEFAULT_LOGS_PATH);

        std::string s = strip_path_copy(script_path);
        //strip_path(s);
        entityLog += s;
        try
        {
            std::vector<spdlog::sink_ptr> sinks;
            //auto rotating_logger = spd::rotating_logger_mt(s, entityLog, 1048576 * 5, 3);
            auto rotating = std::make_shared<spdlog::sinks::rotating_file_sink_mt> (entityLog, "log", 1024*1024, 5, true);
            auto stdout_sink = spdlog::sinks::stdout_sink_mt::instance();
            
            //auto color_sink = std::make_shared<spdlog::sinks::ansicolor_sink>(stdout_sink);
            
           // auto stdout_sink = spdlog::sinks::stdout_sink_mt::instance();
           // auto color_sink = std::make_shared<spdlog::sinks::ansicolor_sink>(stdout_sink);
            sinks.push_back(stdout_sink);
            sinks.push_back(rotating);
            
            auto combined_logger = std::make_shared<spdlog::logger>(s, begin(sinks), end(sinks));
            combined_logger->set_pattern("[%x %H:%M:%S:%e] %v");
            combined_logger->set_level( spdlog::level::debug );
            spdlog::register_logger(combined_logger);

            combined_logger->debug( "[" + this->GetInstancePath() + "] "+ msg);
            combined_logger->flush();
            
        }
        catch( std::exception& )
        {
            
        }
       
   }
   else
   {
       log->debug( "[" + this->GetInstancePath() + "] "+ msg);
       log->flush();
       // spd::drop( strip_path_copy(script_path) );
   }
    // for (int i = 0; i < 10; ++i)
    //rotating_logger->info(msg);
   // rotating_logger->flush();
    //}

    //  std::stringstream ss;
    //  ss << this->GetInstancePath() << " " << msg;
    // std::string d = ss.str();
    //  entity_manager::Instance().debug(d);
}
*/

void script_entity::debug(const std::string& msg)
{

   
    auto log = spd::get(strip_path_copy(virtual_script_path));
    
    if( !log )
    {
        
        std::string entityLog = global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH) +
                            global_settings::Instance().GetSetting(DEFAULT_LOGS_PATH);

        std::string s = strip_path_copy(virtual_script_path);

        entityLog += s;
        try
        {
            std::vector<spdlog::sink_ptr> sinks;

            auto rotating = std::make_shared<spdlog::sinks::rotating_file_sink_mt> (entityLog, "log", 1024*1024, 5, true);
            auto stdout_sink = spdlog::sinks::stdout_sink_mt::instance();
            
            sinks.push_back(stdout_sink);
            sinks.push_back(rotating);
            
            auto combined_logger = std::make_shared<spdlog::logger>(s, begin(sinks), end(sinks));
            combined_logger->set_pattern("[%x %H:%M:%S:%e] %v");
            combined_logger->set_level( spdlog::level::debug );
            spdlog::register_logger(combined_logger);

            combined_logger->debug( "[" + this->GetInstancePath() + "] "+ msg);
            combined_logger->flush();
            
        }
        catch( std::exception& )
        {
            
        }
       
   }
   else
   {
       /*
       switch( ll )
       {
           case LogLevel::CRITICAL:
           {
               log->critical( "[" + this->GetInstancePath() + "] "+ msg);
           }
           break;
           case LogLevel::ERROR:
           {
               log->error( "[" + this->GetInstancePath() + "] "+ msg);
           }
           break;
           case LogLevel::DEBUG:
           {
               log->debug( "[" + this->GetInstancePath() + "] "+ msg);
           }
           break;
           case LogLevel::INFO:
           {
               log->info( "[" + this->GetInstancePath() + "] "+ msg);
           }
           break;
           default:
           break;
       }
        */
       log->debug( "[" + this->GetInstancePath() + "] "+ msg);
       log->flush();
   }

}

void script_entity::SetVirtualScriptPath(std::string& path)
{
    virtual_script_path = path;

    
}

void script_entity::SetPhysicalScriptPath(std::string& path)
{
    physical_script_path = path;
}