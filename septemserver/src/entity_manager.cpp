#include "stdafx.h"
#include <algorithm>

#include "entity_manager.h"
#include "io/db_interface.h"
//#include <boost/filesystem/operations.hpp>
//#include <boost/filesystem/path.hpp>
//#include <boost/filesystem/fstream.hpp>
//#include <boost/algorithm/string.hpp>

namespace spd = spdlog;

namespace fs = boost::filesystem;

void get_entity_str(EntityType etype, std::string& str)
{

    switch(etype) {
    case EntityType::DAEMON: {
        str = "DAEMON";
    } break;
    case EntityType::COMMAND: {
        str = "COMMAND";
    } break;
    case EntityType::ITEM: {
        str = "ITEM";
    } break;
    case EntityType::NPC: {
        str = "NPC";
    } break;
    case EntityType::PLAYER: {
        str = "PLAYER";
    } break;
    case EntityType::ROOM: {
        str = "ROOM";
    } break;
    case EntityType::LIB: {
        str = "LIB";
    } break;
    default:
        str = "UNKNOWN";
        break;
    }
}


bool
get_entity_path_from_id_string(const std::string& entity_id, std::string& entity_script_path, unsigned int& instanceid)
{
    unsigned int tmp = 0;
    entity_script_path = boost::to_lower_copy(entity_id);
    std::size_t found = entity_script_path.find("id=");

    if(found != std::string::npos) {
        // the id has a id= in it..

        sscanf(entity_script_path.c_str(), "%*[^=]=%u", &tmp);
        entity_script_path.erase(entity_script_path.begin() + found,
                                 entity_script_path.end()); // remove it from the string
    }
    if(entity_script_path[entity_script_path.size() - 1] == '/' ||
       entity_script_path[entity_script_path.size() - 1] == ':') // no need to have this.
    {
        entity_script_path.erase(entity_script_path.size() - 1);
    }

    return true;
}

entity_manager::entity_manager()
{
    io_serv = NULL;
}

bool entity_manager::compile_virtual_file(std::string& relative_script_path,
                                          EntityType etype,
                                          std::string& script_text,
                                          std::string& reason)
{
    return compile_entity(relative_script_path, etype, script_text, reason);
}

bool entity_manager::compile_and_clone(std::string& relative_script_path,
                                       std::string& script_path_virtual,
                                       std::string& tag_text,
                                       std::string& tag_replace,
                                       std::string& addl_lua,
                                       std::string& reason)
{
    // return compile_entity(relative_script_path, etype, script_text, reason);

    std::string _fullpath = global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH);
    _fullpath += relative_script_path;

    try {
        if(!fs::exists(_fullpath)) {
            reason = "Unable to locate file: " + _fullpath;
            return false;
        }

        EntityType et;
        std::string _scriptext;
        if(!load_script_text(_fullpath, _scriptext, et, reason)) {
            return false;
        }

        if(tag_text.size() != 0) {
            boost::replace_all(_scriptext, tag_text, tag_replace);
        }
        // _scriptext = _scriptext + "\n_INTERNAL_SCRIPT_PATH_BASE_ = '"+relative_script_path+"'"; //may need to rethink
        // this
        // std::cout << _scriptext << std::endl;
        if(!compile_entity(script_path_virtual, et, _scriptext, reason))
            return false;

    } catch(std::exception& ex) {
        reason = ex.what();
        return false;
    }
    return true;
}

bool entity_manager::compile_entity(std::string& relative_script_path,
                                    EntityType etype,
                                    std::string& script_text,
                                    std::string& reason)
{
    // get the lua stack lock...
    // std::cout << "GETTING LOCK..." << std::endl;
    // std::unique_lock<std::mutex> lock(lua_mutex_);
    // std::cout << "OK GOT LOCK..." << std::endl;

    bool b = false;
    switch(etype) {
    case EntityType::ROOM: {
        // Check for existing rooms
        std::set<std::shared_ptr<entity_wrapper> > rooms;
        get_rooms_from_path(relative_script_path, rooms);
        std::map<std::string, std::vector<playerobj*> > _pents;

        for(auto& r : rooms) {
            roomobj* rp = dynamic_cast<roomobj*>(r->script_ent);
            for(auto pe : rp->GetInventory()) {
                if(auto t = dynamic_cast<playerobj*>(pe)) {
                    t->SetEnvironment(NULL);
                    _pents[rp->GetInstancePath()].push_back(t);
					rp->RemoveEntityFromInventory(t);
                }
				else if(auto t = dynamic_cast<itemobj*>(pe)) {
					t->SetEnvironment(NULL);
					t->_unload_inventory_();
					//deregister_item(t);
					destroy_item(t);
					//m_entity_cleanup.push_back(t);
				}
				else if(auto t = dynamic_cast<npcobj*>(pe)) {
					t->SetEnvironment(NULL);
					t->unload_inventory_from_game();
					//t->set_destroy(true);
					////deregister_npc(t);
					destroy_npc(t);
					////m_entity_cleanup.push_back(t);
				}				

            }
            //deregister_room(rp);
			//destroy_room(rp);
			//m_entity_cleanup.push_back(rp);
        }
		
		destroy_room(relative_script_path);
		//agarbage_collect();
		//garbage_collect();
		//for ( auto )
       // if(rooms.size() > 0) {
        //    destroy_room(relative_script_path);
        //}
        sol::environment to_load;
        _init_entity_env(relative_script_path, etype, to_load);
        b = lua_safe_script(script_text, to_load, reason);
        if(!b) {
            std::stringstream ss;
            ss << "Error compiling script <<  " << relative_script_path
               << ", reason = " << reason; // Destroyed object, script path= " << virtual_script_path;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_ERROR, ss.str());
            if(auto p = security_context::Instance().GetPlayerEntity()) {
                if(p->isCreator()) {
                    p->SendToEntity(ss.str());
                }
            }
            //p->SendToEntity(std::string("Error: ") + reason);
            // TODO: we have a problem, move players to void.. or shutdown server is void is borked too
        } else {
            rooms.clear();
            get_rooms_from_path(relative_script_path, rooms);

            for(auto& k : _pents) {
                for(auto v : k.second) {
                    // for each player ..
                    // try to move them..
                    if(!move_living(dynamic_cast<script_entity*>(v), k.first)) {
                        // TODO: move them to void, or fail.
                    } else {
                        v->SendToEntity("A paralyzing sense of vertigo overtakes you.. and then quickly passes.");
                    }
                }
            }
			for( auto& p : rooms )
			{
				p->script_ent->invoke_on_load();
			}
        }

    } break;
    case EntityType::NPC:
	{
		sol::environment to_load;
        _init_entity_env(relative_script_path, etype, to_load);
        b = lua_safe_script(script_text, to_load, reason);
		if(b)
		{
			std::set< std::shared_ptr<entity_wrapper> > npcs;
			get_npcs_from_path(relative_script_path, npcs);
			for( auto& n: npcs )
			{
				n->script_ent->invoke_on_load();
			}
		}
		break;
	}
    case EntityType::PLAYER: {
        // TODO: logic that finds the player and reconnects them with their client object, if they have an object in the
        // world
        sol::environment to_load;
        _init_entity_env(relative_script_path, etype, to_load);
        b = lua_safe_script(script_text, to_load, reason);
		
    } break;
    case EntityType::DAEMON: {

        std::set<std::shared_ptr<entity_wrapper> > daemons;
        get_daemons_from_path(relative_script_path, daemons);

		for(auto& r : daemons) {
			destroy_daemon(r->script_ent);
        // script_entity& self = r->script_ent.value().selfobj.as<script_entity>();
        //*self = NULL;
        // r->script_ent.value().selfobj = NULL;
        // r->script_env.value() = sol::nil;
        //(*r->script_state).collect_garbage();
        }
        //if(daemons.size() > 0)
         //   destroy_daemon(relative_script_path);
        sol::environment to_load;
        _init_entity_env(relative_script_path, etype, to_load);
        b = lua_safe_script(script_text, to_load, reason);
		if(b)
		{
			std::set< std::shared_ptr<entity_wrapper> > ds;
			get_daemons_from_path(relative_script_path, ds);
			for( auto& d: ds )
			{
				d->script_ent->invoke_on_load();
			}
		}
    } break;
    case EntityType::ITEM: {
        sol::environment to_load;
        _init_entity_env(relative_script_path, etype, to_load);
        b = lua_safe_script(script_text, to_load, reason);
		if(b)
		{
			std::set< std::shared_ptr<entity_wrapper> > ds;
			get_npcs_from_path(relative_script_path, ds);
			for( auto& d: ds )
			{
				d->script_ent->invoke_on_load();
			}
		}
        /*
        // TODO.. fix updates to items so we recompile them..
        if( b )
        {
            for( auto ie : m_item_objs )
            {
                //std::cout << ie.first << std::endl;
                for( auto ew : ie.second )
                {
                    entity_wrapper * ewp = ew.get();
                    if( ewp->script_ent->GetBaseScriptPath() == relative_script_path )
                    {
                        // TODO: fix this
                        // 1. find items matching.  this will get hairy if its a container. what if that container
                        // has a container in it.. and that container has a container...  and then what if the container
                        // within the container needs to be recompiled too.
                        //clone_item( ewp->script_ent->GetBaseScriptPath(),
                        //std::string& relative_script_path, script_entity* obj, std::string uid)
                        //std::cout << "MATCH " << ewp->script_ent->GetScriptPath() << std::endl;
                    }
                }

            }
        }
        */
    } break;
    case EntityType::COMMAND: {
		std::set<std::shared_ptr<entity_wrapper> > cmds;
        get_cmds_from_path(relative_script_path, cmds);

		for(auto& r : cmds) {
			destroy_command(r->script_ent);
		}
			
        sol::environment to_load;
        _init_entity_env(relative_script_path, etype, to_load);
        b = lua_safe_script(script_text, to_load, reason);
    } break;
    case EntityType::LIB: {
        return lua_safe_script(script_text, (*m_state).globals(), reason);
    } break;

    default:
        break;
    }
    return b;
}

itemobj* entity_manager::clone_item_to_hand(std::string& relative_script_path, handobj* obj, std::string uid)
{
    return clone_item(relative_script_path, obj->GetOwner(), uid);
}

itemobj* entity_manager::clone_item_to_inventory(std::string& relative_script_path, script_entity* obj, std::string uid)
{
    return clone_item(relative_script_path, obj, uid);
}

npcobj* entity_manager::clone_npc_to_room(std::string& relative_script_path, roomobj* r, std::string uid)
{
    
    if( relative_script_path.empty() )
        return NULL;
    if( relative_script_path[0] == '/' )
    {
        relative_script_path.erase(0, 1);
    }
    
    return clone_npc(relative_script_path, r, uid);
}

bool entity_manager::do_item_reload(std::string& entitypath, playerobj* p)
{
    if(!security_context::Instance().isArch()) {
        p->SendToEntity("Sorry, you lack the privileges to perform this command.");
        return false;
    }
    std::string temp = entitypath;
    std::string reason;
    if(!fs_manager::Instance().translate_path(temp, p, reason)) {
        p->SendToEntity(reason);
        return false;
    }
    if(fs::is_directory(temp)) {
        p->SendToEntity("Can't reload a directory... not implemented yet.");
        return false;
    }
    std::string relative_path =
        std::regex_replace(temp,
                           std::regex("\\" + global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH)),
                           ""); // remove the extra pathing stuff we don't
    bool b = reload_all_item_instances(relative_path);
    return b;
}

bool entity_manager::reload_all_item_instances(std::string& relative_script_path)
{

    struct _item_
    {
        std::string uid;
        std::string script_path;
        std::string json;
    };

    struct _item_index_
    {
        std::string uid;

        std::string script_path;
        std::string parent_env_path;
        std::string parent_env_uid;
        std::string parent_name;
        EntityType parent_type;
        bool isContainer;
        handobj* h;
        // bool bisHand;
        itemobj* io;
        std::map<std::string, _item_> inventory;
        std::string json;
    };

    std::vector<std::shared_ptr<_item_index_> > items;

    for(auto ie : m_item_objs) {
        for(auto ew : ie.second) {
            entity_wrapper* ewp = ew.get();
            std::string s = ewp->script_ent->GetPhysicalScriptPath();

            if(ewp->script_ent->GetPhysicalScriptPath() == relative_script_path) {
                itemobj* t = static_cast<itemobj*>(ewp->script_ent);

                std::shared_ptr<_item_index_> p(new _item_index_);
                p->uid = t->get_uid();
                p->script_path = t->GetPhysicalScriptPath();
                p->io = t;
                if(t->GetEnvironment()) {
                    p->parent_env_path = t->GetEnvironment()->GetVirtualScriptPath();
                    p->parent_type = t->GetEnvironment()->GetType();
                    p->parent_env_uid = t->GetEnvironment()->get_uid();
                    p->parent_name = t->GetEnvironment()->GetName();

                    if(itemobj* ie = dynamic_cast<itemobj*>(t->GetEnvironment())) {
                        ie->RemoveEntityFromInventory(ewp->script_ent);
                    } else if(roomobj* ro = dynamic_cast<roomobj*>(t->GetEnvironment())) {
                        ro->RemoveEntityFromInventory(ewp->script_ent);
                    } else if(living_entity* le = dynamic_cast<living_entity*>(t->GetEnvironment())) {
                        le->RemoveEntityFromInventory(ewp->script_ent);
                    } else if(handobj* ho = dynamic_cast<handobj*>(t->GetEnvironment())) {
                        ho->RemoveEntityFromInventory(ewp->script_ent);
                        p->h = ho;
                    }
                }
                for(auto i : t->GetItems()) {
                    // std::cout << "Adding item to destroy " << i->GetVirtualScriptPath()<< std::endl;
                    _item_ i_temp;
                    i_temp.script_path = i->GetPhysicalScriptPath();
                    i_temp.uid = i->get_uid();
                    i_temp.json = i->Serialize();
                    p->inventory.insert({ i->get_uid(), i_temp });
                }

                p->json = t->Serialize();
                items.push_back(p);
            }
        }
    }

    for(auto k : items) {
        std::string instance_path = k->io->GetVirtualScriptPath();
        destroy_item(instance_path);
        // destroy items in the inventory too..
        for(auto kvp : k->inventory) {
            // p->inventory.insert( { t->get_uid(),  i->GetBaseScriptPath() } );
            // std::cout << "Trying to destroy " << kvp.second << std::endl;
            destroy_item(kvp.second.script_path);
        }
    }

    for(auto k : items) {
        // this->clone_item( k.second.script_path, )
        switch(k->parent_type) {
        case EntityType::ITEM: {
            itemobj* io = this->GetItemByScriptPath(k->parent_env_path);
            assert(io != NULL);
            script_entity* e_tmp = clone_item(k->script_path, dynamic_cast<script_entity*>(io), k->uid);
            assert(e_tmp != NULL);
            io->AddEntityToInventory(e_tmp);
            if(!e_tmp->do_json_load(k->json)) {
                std::stringstream ss;
                ss << "Error while restoring persisted object settings, script= " << e_tmp->GetInstancePath();
                // debug( ss.str() );
            }

            if(!e_tmp->do_save()) {
                std::stringstream ss;
                ss << "Error while restoring persisted object settings, script= " << e_tmp->GetInstancePath();
                //  debug( ss.str() );
            }

        } break;
        case EntityType::ROOM: {
            roomobj* r = this->GetRoomByScriptPath(k->parent_env_path);
            assert(r != NULL);
            script_entity* e_tmp = clone_item(k->script_path, dynamic_cast<script_entity*>(r), k->uid);
            assert(e_tmp != NULL);
            r->AddEntityToInventory(e_tmp);
            if(!e_tmp->do_json_load(k->json)) {
                std::stringstream ss;
                ss << "Error while restoring persisted object settings, script= " << e_tmp->GetInstancePath();
                // debug( ss.str() );
            }

            if(!e_tmp->do_save()) {
                std::stringstream ss;
                ss << "Error while restoring persisted object settings, script= " << e_tmp->GetInstancePath();
                //  debug( ss.str() );
            }

        } break;
        case EntityType::PLAYER: {
            playerobj* p = this->get_player(k->parent_name);
            assert(p != NULL);
            script_entity* e_tmp = clone_item(k->script_path, dynamic_cast<script_entity*>(p), k->uid);
            assert(e_tmp != NULL);
            p->AddEntityToInventory(e_tmp);
            if(!e_tmp->do_json_load(k->json)) {
                std::stringstream ss;
                ss << "Error while restoring persisted object settings, script= " << e_tmp->GetInstancePath();
                // debug( ss.str() );
            }

            if(!e_tmp->do_save()) {
                std::stringstream ss;
                ss << "Error while restoring persisted object settings, script= " << e_tmp->GetInstancePath();
                //  debug( ss.str() );
            }
        } break;
        case EntityType::HAND: {
            handobj* p = k->h;
            assert(p != NULL);
            script_entity* e_tmp = clone_item(k->script_path, dynamic_cast<script_entity*>(p), k->uid);
            assert(e_tmp != NULL);
            p->AddEntityToInventory(e_tmp);
            if(!e_tmp->do_json_load(k->json)) {
                std::stringstream ss;
                ss << "Error while restoring persisted object settings, script= " << e_tmp->GetInstancePath();
                // debug( ss.str() );
            }

            if(!e_tmp->do_save()) {
                std::stringstream ss;
                ss << "Error while restoring persisted object settings, script= " << e_tmp->GetInstancePath();
                //  debug( ss.str() );
            }
        } break;
        case EntityType::NPC: {

        } break;
        default:
            return false;
            break;
        }
    }

    return true;
}

itemobj* entity_manager::clone_item(std::string& relative_script_path, script_entity* obj, std::string uid)
{
    std::stringstream ss;
    ss << "Cloning entity " << relative_script_path << ", id = " << uid;

    if(uid.size() == 0)
        uid = random_string();
    std::string reason;
    std::string tag = "";
    std::string vpath = relative_script_path + "/" + uid;
    std::string adl_lua = "\n_ENTITY_ENV_PATH_ = '" + obj->GetInstancePath() + "'";
    adl_lua += "\n_ENTITY_ENV_TYPE_ = '" + obj->GetEntityTypeString() + "'";
    adl_lua += "\n_INTERNAL_UUID_ = '" + uid + "'"; // may need to rethink this
    if(!this->compile_and_clone(relative_script_path, vpath, tag, uid, adl_lua, reason)) {

        if(obj != NULL)
            obj->debug(reason);
        if(auto p = security_context::Instance().GetPlayerEntity()) {
            if(p->isCreator()) {
                std::stringstream ss;
                ss << "Error cloning entity: " << relative_script_path << ", reason: " << reason;
                p->SendToEntity(ss.str());
            }
        }
        return NULL;
    } else {
        itemobj* i = GetItemByScriptPath(vpath);

        if(i != NULL) {
            // i->SetPhysicalScriptPath(relative_script_path);
            i->set_uid(uid);
            relative_script_path = vpath;

            if(auto e = dynamic_cast<container_base*>(obj)) {
                e->AddEntityToInventory(i);
                std::stringstream ss;
                ss << "Cloning entity " << relative_script_path << ", id = " << uid;
                obj->debug(ss.str());
                return i;
            } else {
                obj->debug("Error moving cloned object into inventory. Invalid cast.");
                return NULL;
            }
        }
    }

    return NULL;
}

npcobj* entity_manager::clone_npc(std::string& relative_script_path, roomobj* r, std::string uid)
{
    std::stringstream ss;
    ss << "Cloning npc " << relative_script_path << ", id = " << uid;

    if(uid.size() == 0)
        uid = random_string();
    std::string reason;
    std::string tag = "";
    std::string vpath = relative_script_path + "/" + uid;
    std::string adl_lua = "\n_ENTITY_ENV_PATH_ = '" + r->GetInstancePath() + "'";
    adl_lua += "\n_ENTITY_ENV_TYPE_ = '" + r->GetEntityTypeString() + "'";
    adl_lua += "\n_INTERNAL_UUID_ = '" + uid + "'"; // may need to rethink this
    if(!this->compile_and_clone(relative_script_path, vpath, tag, uid, adl_lua, reason)) {

        if(r != NULL)
            r->debug(reason);
        if(auto p = security_context::Instance().GetPlayerEntity()) {
            if(p->isCreator()) {
                std::stringstream ss;
                ss << "Error cloning npc: " << relative_script_path << ", reason: " << reason;
                p->SendToEntity(ss.str());
            }
        }
        return NULL;
    } else {
        npcobj* i = GetNPCByScriptPath(vpath);

        if(i != NULL) {
            // i->SetPhysicalScriptPath(relative_script_path);
            i->set_uid(uid);
            relative_script_path = vpath;

            if(auto e = dynamic_cast<container_base*>(r)) {
                e->AddEntityToInventory(i);
                std::stringstream ss;
                ss << "Moving NPC to destination .. " << relative_script_path << ", id = " << uid;
                r->debug(ss.str());
                return i;
            } else {
                r->debug("Error moving cloned npc into room. Invalid cast.");
                return NULL;
            }
        }
    }

    return NULL;
}

bool entity_manager::compile_script_file(std::string& file_path, std::string& reason)
{
	std::unique_lock<std::recursive_mutex> lock(lua_mutex_);

    std::stringstream ss;
    ss << "Compiling script: " << file_path;
    log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, file_path, ss.str());

    fs::path p(file_path);
    if(!fs::exists(p)) {
        reason = "Script does not exist.";
        return false;
    }

    std::string script_ = std::regex_replace(
        file_path, std::regex("\\" + global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH)), "");
    //_currently_loading_script = script_;

    if(!m_state) {
        m_state = std::shared_ptr<sol::state>(new sol::state);
        init_lua();
    }
    // lua_safe_script(path, *m_state);
    std::string script_text;
    EntityType etype;
    std::string error_str;

    if(!load_script_text(file_path, script_text, etype, error_str)) {
        reason = error_str;
        return false;
    }

    return compile_entity(script_, etype, script_text, reason);
}

roomobj* entity_manager::get_void_room()
{
    std::stringstream ss;

    std::string void_script_path = global_settings::Instance().GetSetting(DEFAULT_VOID_ROOM);
    unsigned int id = 0;
    roomobj* r = GetRoomByScriptPath(void_script_path, id);
    if(r != NULL) {
        return r;
    } else {
        std::string err = "Error attempting to retrieve void path.";
        ss << err;
        log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ss.str());
        // on_error(err);
    }
    return NULL;
}

bool entity_manager::load_player(std::string playername, bool bloggedIn)
{
    std::string lpname = boost::to_lower_copy(playername);

    playerobj* pplayer = get_player(lpname);

    if(pplayer == NULL) {
        std::string reason;
        if(!compile_player(lpname, reason)) {
            std::stringstream ss;
            ss << "Error loading player: " << playername << ", reason=" << reason << std::endl;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ss.str());
            return false;
        }
    }

    pplayer = get_player(lpname);

    assert(pplayer != NULL);

    if(bloggedIn) {
        pplayer->set_loggedIn(true); // not sure if I want to do this here.. but oh well
        pplayer->do_load();
    } else
        return true; // this stops a temp user that has yet to log in from appearing in game

    if(pplayer->cwd.empty()) {
        pplayer->cwd = "workspaces/" + lpname;
    }

    // pplayer->set_loggedIn(true);
    // if( pplayer->workspacePath.empty() )
    //  {
    //     pplayer->workspacePath = "workspaces/" + lpname;
    // }

    if(pplayer->roomPath.empty()) {
        std::stringstream ss;
        ss << "Moving player " << playername << " to the void.";
		log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ss.str());

        // TODO: Put them into the default room.. or some place else.
        roomobj* r = this->get_void_room();
        assert(r != NULL);
        if(!move_entity(pplayer, r)) {
            std::stringstream ss;
            ss << "Unable to move " << playername;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ss.str());
            return false;
        }
    } else {
        std::stringstream ss;
        ss << "Moving player " << playername << " to " << pplayer->roomPath;
        log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ss.str());

        roomobj* r = GetRoomByScriptPath(pplayer->roomPath, pplayer->roomID);
        // assert(r != NULL);
        if(r == NULL) {
            roomobj* rv = this->get_void_room();
            assert(rv != NULL);
            if(!move_entity(pplayer, rv)) {
                
                std::stringstream ss;
                ss << "Unable to move " << playername;
                log_interface::Instance().log(LOGLEVEL::LOGLEVEL_ERROR, ss.str());
                return false;
            }
        } else if(!move_entity(pplayer, r)) {
            std::stringstream ss;
            ss << "Unable to move " << playername;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_ERROR, ss.str());
            return false;
        }
    }

    return true;
}

bool entity_manager::compile_player(std::string playerName, std::string& reason)
{
    // std::string player_script_path = global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH);
    std::string player_script_path = global_settings::Instance().GetSetting(BASE_PLAYER_ENTITY_PATH);
    // std::string reason;

    std::stringstream ss;
    ss << "entities/player_" << boost::to_lower_copy(playerName);

    std::string tpath = ss.str();
    std::string tagtext = "player_name"; // a hack to get our player name into the lua object constructor
    std::string adllua = "";
    return compile_and_clone(player_script_path, tpath, tagtext, playerName, adllua, reason);
}

bool entity_manager::lua_safe_script(std::string& script_text, sol::environment env, std::string& reason)
{
    sol::state& lua = (*m_state);

    sol::load_result lr = lua.load(script_text);
    if(!lr.valid()) {
        sol::error err = lr;
        reason = err.what();
        return false;
    }

    m_state_internal->_current_script_f_ = lr; // lua.load(script_text);

    env.set_on(m_state_internal->_current_script_f_);

    sol::protected_function_result result = m_state_internal->_current_script_f_();
    if(!result.valid()) {
        sol::error err = result;
        reason = err.what();
        // std::cout << "call failed, sol::error::what() is " << what << std::endl;
        return false;
    }

    return true;
}

template <typename T> T* downcast(script_entity* b)
{
    return dynamic_cast<T*>(b);
}

void entity_manager::init_lua()
{

    sol::state& lua = (*m_state);
    m_state_internal.reset(new _internal_lua_);

    init_lua_state(lua);

    lua.set_function("register_heartbeat", &heartbeat_manager::register_heartbeat_func_on, &_heartbeat);
    lua.set_function("deregister_heartbeat", &heartbeat_manager::deregister_heartbeat_func, &_heartbeat);

    lua.set_function("deregister_all_heartbeat", &heartbeat_manager::deregister_all_heartbeat_funcs, &_heartbeat);

    lua.set_function("move_living", &entity_manager::move_living, this);
    // lua.set_function("capture_input", &entity_manager::capture_input, this);
    // lua.set_function("capture_input", &entity_manager::capture_input, this);

    lua.set_function("command_cast", &downcast<commandobj>);
    lua.set_function("room_cast", &downcast<roomobj>);
    lua.set_function("player_cast", &downcast<playerobj>);
    lua.set_function("item_cast", &downcast<itemobj>);
    lua.set_function("door_cast", &downcast<doorobj>);
    lua.set_function("living_cast", &downcast<living_entity>);
    lua.set_function("int_to_string", &int_to_string);

    lua.set_function("get_default_commands", [&]() -> std::map<std::string, commandobj*> & {
        std::string command_path = global_settings::Instance().GetSetting(DEFAULT_COMMANDS_PATH);
        return m_cmds_map[command_path];
    });

    lua.set_function("get_commands", [&](living_entity * p) -> std::map<std::string, commandobj*> {
        std::vector<std::string>& cmds = p->GetCommandDirs();
        std::map<std::string, commandobj*> player_cmds;
        for(auto const& c : cmds) {
            auto search = m_cmds_map.find(c);
            if(search != m_cmds_map.end()) {
                for(auto const& x : search->second) {
                    player_cmds[x.second->GetCommand()] = x.second;
                }
            }
        }
        return player_cmds;
    });

    lua.set_function("get_players", [&]() -> std::vector<playerobj*> {
        std::vector<playerobj*> players;
        for(auto const& p : m_player_objs) {
			playerobj * p1 = dynamic_cast<playerobj*>(p.second->script_ent);
			if( p1->get_loggedIn() )
				players.push_back(p1);
			//players.push_back(dynamic_cast<playerobj*>(p.second->script_ent));
		}
        return players;
    });
	
	lua.set_function("get_players_by_biome", [&](int bt) -> std::vector<playerobj*> {
        std::vector<playerobj*> players;
        for(auto const& p : m_player_objs) {
			playerobj * p1 = dynamic_cast<playerobj*>(p.second->script_ent);
			if( p1->get_loggedIn() && p1->GetRoom()->GetBiomeType() == bt and p1->GetRoom()->GetIsOutdoor())
				players.push_back(p1);
		}
        return players;
    });
	
	lua.set_function("get_players_by_biome_debug", [&](int bt) -> std::vector<playerobj*> {
        std::vector<playerobj*> players;
        for(auto const& p : m_player_objs) {
			playerobj * p1 = dynamic_cast<playerobj*>(p.second->script_ent);
			if( p1->get_loggedIn() && p1->GetRoom()->GetBiomeType() == bt && p1->isCreator() )
				players.push_back(p1);
		}
        return players;
    });

    lua.set_function("get_move_command", [&]() -> commandobj * & {
        std::string command_path = global_settings::Instance().GetSetting(DEFAULT_COMMANDS_PATH);
        return m_cmds_map[command_path].find("move")->second;
    });

    lua.set_function("get_room_list", [&]() -> std::map<std::string, roomobj*> & { return m_room_lookup; });

    lua.set_function("get_entity_by_name", [&](std::string ename) -> playerobj * { return this->get_player(ename); });

    lua.set_function("get_daemon", [&](std::string patha, unsigned int instanceid) -> daemonobj * {
        return this->GetDaemonByScriptPath(patha, instanceid);
    });

    lua.set_function("get_setting",
                     [&](std::string sname) -> std::string { return global_settings::Instance().GetSetting(sname); });

    lua.set_function("get_dir_list", [&](std::string dir) -> std::vector<file_entity> {
        return fs_manager::Instance().get_dir_list(dir);
    });

    lua.set_function("change_directory", [&](std::string dir, playerobj* p) -> bool {
        return fs_manager::Instance().change_directory(dir, p);
    });

    lua.set_function("do_update", [&](std::string dir, playerobj* p) -> bool { return this->do_update(dir, p); });

    lua.set_function("do_copy", [&](std::string patha, std::string pathb, playerobj* p) -> bool {
        return fs_manager::Instance().do_copy(patha, pathb, p);
    });

    lua.set_function("do_remove",
                     [&](std::string path, playerobj* p) -> bool { return fs_manager::Instance().do_remove(path, p); });

    lua.set_function("do_goto", [&](std::string path, playerobj* p) -> bool { return this->do_goto(path, p); });

    lua.set_function("do_tp",
                     [&](std::string path, playerobj* p1, playerobj* p2) -> bool { return this->do_tp(path, p1, p2); });

    lua.set_function("do_promote", [&](playerobj* p1, std::string p2) -> bool {
        if(!security_context::Instance().isArch()) {
            return false;
        } else
            return this->do_promote(p1, p2);

    });

    lua.set_function("clone_item",
                     [&](std::string path, script_entity * e) -> itemobj * { return this->clone_item(path, e); });

    lua.set_function("do_reload",
                     [&](std::string path, playerobj* p) -> bool { return this->do_item_reload(path, p); });

    lua.set_function("clone_item_to_hand",
                     [&](std::string path, handobj * e) -> itemobj * { return this->clone_item_to_hand(path, e); });
					 
    lua.set_function("clone_item_to_inventory",
                     [&](std::string path, script_entity * e) -> itemobj * 
					 { return this->clone_item_to_inventory(path, e); });
                     
    lua.set_function("clone_npc_to_room",
                     [&](std::string path, roomobj * r) -> npcobj * { return this->clone_npc_to_room(path, r); });
					 
	lua.set_function("unload_npc",
                     [&](npcobj * n) -> void * { this->unload_npc(n); });

    lua.set_function("do_translate_path",
                     [&](std::string path, playerobj* e) -> std::string { return this->do_translate_path(path, e); });

    lua.set_function("get_account_exists",
                     [&](std::string sname) -> bool { return account_manager::Instance().get_accountExists(sname); });
					 
	lua.set_function("get_player",
                     [&]() -> script_entity *  { return security_context::Instance().GetCurrentEntity(); });

    lua.set_function("create_new_account", [&](std::string aname, std::string pass, std::string email, int eg) -> bool {
        if(!security_context::Instance().isArch()) {
            return false;
        } else
            return account_manager::Instance().create_account(aname, pass, email, eg);
    });
    // lua.set_function( "capture_input", [&](std::string path, script_entity * e) -> itemobj * { return
    // this->clone_item(path, e); });

    // lua.set_function("tail_entity_log",
    //                 [&](script_entity * se) -> std::vector<std::string> & { return this->get_player(ename); });

    // sol::environment tmp = sol::environment(lua, sol::create); // inherit);
    // lua["_g_mirror_"] = tmp;

    /*
        sol::environment global_env = lua.globals();
        sol::environment base_env;

        _init_lua_env_(lua, global_env, global_env, "base_entity_", base_env);

        sol::environment daemon_env;
        _init_lua_env_(lua, global_env, base_env, get_env_str(EntityType::DAEMON), daemon_env);

        sol::environment command_env;
        _init_lua_env_(lua, global_env, base_env, get_env_str(EntityType::COMMAND), command_env);

        sol::environment item_env;
        _init_lua_env_(lua, global_env, base_env, get_env_str(EntityType::ITEM), item_env);

        sol::environment npc_env;
        _init_lua_env_(lua, global_env, base_env, get_env_str(EntityType::NPC), npc_env);

        sol::environment player_env;
        _init_lua_env_(lua, global_env, base_env, get_env_str(EntityType::PLAYER), player_env);

        sol::environment room_env;
        _init_lua_env_(lua, global_env, base_env, get_env_str(EntityType::ROOM), room_env);
    */
}

std::string get_str_between(const std::string &s,
        const std::string &start_delim,
        const std::string &stop_delim)
{
    unsigned first_delim_pos = s.find(start_delim);
    unsigned end_pos_of_first_delim = first_delim_pos + start_delim.length();
    unsigned last_delim_pos = s.find(stop_delim);

    return s.substr(end_pos_of_first_delim,
            last_delim_pos - end_pos_of_first_delim);
}

bool entity_manager::load_script_text(std::string& script_path,
                                      std::string& script_text,
                                      EntityType& obj_type,
                                      std::string& reason)
{
    std::ifstream in(script_path);
    std::stringstream buffer;
    buffer << in.rdbuf();

    std::string token;
    std::vector<std::string> file_tokens;
    std::vector<std::string> include_tokens;
    // PRE-PROCESSOR STUFF
    bool bFoundType = false;
    while(std::getline(buffer, token, '\n')) {
        trim(token);
        if( boost::starts_with(token, "#include" ) )
        {
            std::string d1 = "<";
            std::string d2 = ">";
            std::string d3 = "\"";
            std::string tinclude = get_str_between(token, d1, d2 );
            bool ifound = false;
            if( !tinclude.empty() )
            {
                include_tokens.push_back(tinclude);
                ifound = true;
            }
            tinclude = get_str_between(token, d3, d3 );
            if( !tinclude.empty() )
            {
                include_tokens.push_back(tinclude);
                ifound = true;
            }
            if( !ifound )
            {
                reason = "Include directive is empty or malformed.";
                return false;
            }
            token = "--" + token + "\r\n";
            file_tokens.push_back(token);
        }
        if(token.compare("inherit lib") == 0) {
            if(bFoundType) // bad. only one type per script
            {
                reason = "Multiple inherit directives detected. Only one entity type is allowed per script.";
                return false;
            }
            obj_type = EntityType::LIB;
            token = "--" + token + "\r\n";
            file_tokens.push_back(token);

            bFoundType = true;
        } else if(token.compare("inherit daemon") == 0) {
            if(bFoundType) // bad. only one type per script
            {
                reason = "Multiple inherit directives detected. Only one entity type is allowed per script.";
                return false;
            }
            obj_type = EntityType::DAEMON;
            token = "--" + token + "\r\n";
            file_tokens.push_back(token);

            bFoundType = true;
        } else if(token.compare("inherit room") == 0) {
            if(bFoundType) // bad. only one type per script
            {
                reason = "Multiple inherit directives detected. Only one entity type is allowed per script.";
                return false;
            }
            obj_type = EntityType::ROOM;
            token = "--" + token + "\r\n";
            file_tokens.push_back(token);

            bFoundType = true;
        } else if(token.compare("inherit player") == 0) {
            if(bFoundType) // bad. only one type per script
            {
                reason = "Multiple inherit directives detected. Only one entity type is allowed per script.";
                return false;
            }
            obj_type = EntityType::PLAYER;
            token = "--" + token + "\r\n";
            file_tokens.push_back(token);
            bFoundType = true;
        } else if(token.compare("inherit item") == 0) {
            if(bFoundType) // bad. only one type per script
            {
                reason = "Multiple inherit directives detected. Only one entity type is allowed per script.";
                return false;
            }
            obj_type = EntityType::ITEM;
            token = "--" + token + "\r\n";
            file_tokens.push_back(token);
            bFoundType = true;
        } else if(token.compare("inherit npc") == 0) {
            if(bFoundType) // bad. only one type per script
            {
                reason = "Multiple inherit directives detected. Only one entity type is allowed per script.";
                return false;
            }
            obj_type = EntityType::NPC;
            token = "--" + token + "\r\n";
            file_tokens.push_back(token);

            bFoundType = true;
        } else if(token.compare("inherit command") == 0) {
            if(bFoundType) // bad. only one type per script
            {
                reason = "Multiple inherit directives detected. Only one entity type is allowed per script.";
                return false;
            }
            obj_type = EntityType::COMMAND;
            token = "--" + token + "\r\n";
            file_tokens.push_back(token);

            bFoundType = true;
        } else {
            file_tokens.push_back(token);
        }
    }
    script_text += "\n_INTERNAL_PHYSICAL_SCRIPT_PATH_ = '" +
                   std::regex_replace(
                       script_path, std::regex(global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH)), "") +
                   "'\n";
    script_text += vector_to_string(file_tokens, '\n');

    // std::cout << script_text << std::endl;
    if(bFoundType == false) {
        reason = "No inherit directive found in script.";
        return false;
    }
    return true;
}
/*
bool entity_manager::get_room_by_script(std::string& script_path, std::unordered_set<entity_wrapper>& room_ents)
{
    auto search = m_room_objs.find(script_path);
    if(search == m_room_objs.end()) {
        return false;
    }
    room_ents = search->second;
    return true;
}
*/

bool entity_manager::unload_player(const std::string& playername)
{
    std::unique_lock<std::recursive_mutex> lock(lua_mutex_);

    playerobj* po = get_player(playername);

    if(po) {
        // Make sure we remove any outstanding elements in the queue.
        // otherwise we can have a crash
        {
            std::unique_lock<std::mutex> lock(dispatch_queue_mutex_);
            dispatch_queue_.remove_if([po](auto& i) { return i.ent == po; });
        }

    } else {
        return false;
    }

   // std::unique_lock<std::recursive_mutex> lock(lua_mutex_);
    po->unload_inventory_from_game(); // Make sure to kill all items..
    std::string ppath = po->GetVirtualScriptPath();

    bool b = destroy_player(ppath);
    garbage_collect();

    return b;
}

bool entity_manager::unload_npc(npcobj * npc)
{
    std::unique_lock<std::recursive_mutex> lock(lua_mutex_);

	// Make sure we remove any outstanding elements in the queue.
	// otherwise we can have a crash
	{
		std::unique_lock<std::mutex> lock(dispatch_queue_mutex_);
		dispatch_queue_.remove_if([npc](auto& i) { return i.ent == npc; });
	}
	
   // npc->unload_inventory_from_game(); // Make sure to kill all items..
   // std::string ppath = npc->GetVirtualScriptPath();
	//if( npc->GetEnvironment() )
	//{
	//	dynamic_cast<container_base*>(npc->GetEnvironment())->RemoveEntityFromInventory(npc);
	//}
	//do_delete(npc);
    //garbage_collect();

    return true;
}


bool entity_manager::destroy_player(std::string& script_path)
{
    // sol::state& lua = (*m_state);

    {
        //

        auto search = m_player_objs.find(script_path);
        if(search == m_player_objs.end()) {
            return false;
        }

        script_entity* e = search->second->script_ent->GetEnvironment();
        if(e) {
            auto* pp = dynamic_cast<playerobj*>(search->second->script_ent);
            pp->GetRoom()->RemoveEntityFromInventory(pp);
        }

        sol::environment env;
        std::string name;
        get_parent_env_of_entity(script_path, env, name);

        destroy_entity(search->second);

        env[name] = sol::nil;
        m_player_objs.erase(search);
    }

    //  lua.collect_garbage();

    return true;
}

bool entity_manager::do_delete(script_entity* se)
{
	if( auto e = dynamic_cast<roomobj*>(se) )
	{
		destroy_room(se);
	}
	else if( auto e = dynamic_cast<npcobj*>(se) )
	{
		destroy_npc(se);
	}
	else if( auto e = dynamic_cast<itemobj*>(se) )
	{
		destroy_item(se);
	}
	else if( auto e = dynamic_cast<commandobj*>(se) )
	{
		destroy_command(se);
	}
	else if( auto e = dynamic_cast<daemonobj*>(se) )
	{
		destroy_daemon(se);
	}
	return true;
}

bool entity_manager::destroy_room(std::string& script_path)
{

    // sol::state& lua = (*m_state);
    {
        auto search = m_room_objs.find(script_path);
        if(search == m_room_objs.end()) {
            return false;
        }

        sol::environment env;
        std::string name;
        get_parent_env_of_entity(script_path, env, name);

        _heartbeat.clear_heartbeat_funcs(script_path);

        for(auto e : search->second) {
			//deregister_room(dynamic_cast<roomobj*>(e->script_ent));
            destroy_entity(e);
        }

        env[name] = sol::nil;
        m_room_objs.erase(search);
    }

    // lua.collect_garbage();

    return true;
}

bool entity_manager::destroy_room(script_entity* ent)
{
	std::string script_path = ent->GetVirtualScriptPath();
	//ent->set_destroy(true);
	//std::string script_path="";
	//unsigned int instance = 0;
	//get_entity_path_from_id_string( ent->GetInstancePath(), script_path, instance);
    {
        auto search = m_room_objs.find(script_path);
        if(search == m_room_objs.end()) {
            return false;
        }

        sol::environment env;
        std::string name;
        get_parent_env_of_entity(script_path, env, name);

		auto i = std::begin(search->second);

		while (i != std::end(search->second)) {
			if ((*i)->script_ent == ent )
			{
				std::shared_ptr<entity_wrapper> ew = *i;
				destroy_entity(ew);
				i = search->second.erase(i);
			}
			else
				++i;
		}
		/*
        for(auto e : search->second) {
			if( e->script_ent == ent )
			{
				 destroy_entity(e);
				 
			}
			//deregister_item(dynamic_cast<itemobj*>(e->script_ent));
        }
		*/
		if( search->second.size() == 0 )
		{
            std::stringstream ss;
            ss << "Destroyed environment " << script_path;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ss.str());
			env[name] = sol::nil;
			//m_room_objs.erase(search);
		
		}
    }

    //(*m_state).collect_garbage();
    //(*m_state).collect_garbage();

    return true;
}

bool entity_manager::destroy_command(std::string& script_path)
{

    // sol::state& lua = (*m_state);
    {
        auto search = m_cmd_objs.find(script_path);
        if(search == m_cmd_objs.end()) {
            return false;
        }

        sol::environment env;
        std::string name;
        get_parent_env_of_entity(script_path, env, name);

        for(auto e : search->second) {
            //deregister_command(dynamic_cast<commandobj*>(e->script_ent));
            destroy_entity(e);
        }

        env[name] = sol::nil;
        m_cmd_objs.erase(search);
    }

    // lua.collect_garbage();

    return true;
}

bool entity_manager::destroy_command(script_entity* ent)
{
	std::string script_path = ent->GetVirtualScriptPath();
	//unsigned int instance = 0;
	//get_entity_path_from_id_string( ent->GetInstancePath(), script_path, instance);
    {
        auto search = m_cmd_objs.find(script_path);
        if(search == m_cmd_objs.end()) {
            return false;
        }

        sol::environment env;
        std::string name;
        get_parent_env_of_entity(script_path, env, name);

		auto i = std::begin(search->second);

		while (i != std::end(search->second)) {
			if ((*i)->script_ent == ent )
			{
				std::shared_ptr<entity_wrapper> ew = *i;
				destroy_entity(ew);
				i = search->second.erase(i);
			}
			else
				++i;
		}

		if( search->second.size() == 0 )
		{
			
            std::stringstream ss;
            ss << "Destroyed environment " << script_path;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ss.str());
			env[name] = sol::nil;
		}
    }

    // lua.collect_garbage();

    return true;
}



bool entity_manager::destroy_item(std::string& script_path)
{
	

    // sol::state& lua = (*m_state);
    {
        auto search = m_item_objs.find(script_path);
        if(search == m_item_objs.end()) {
            return false;
        }

        sol::environment env;
        std::string name;
        get_parent_env_of_entity(script_path, env, name);

        for(auto e : search->second) {
			//deregister_item(dynamic_cast<itemobj*>(e->script_ent));
            destroy_entity(e);
        }

        env[name] = sol::nil;
        m_item_objs.erase(search);
    }

    // lua.collect_garbage();

    return true;
}

bool entity_manager::destroy_item(script_entity* ent)
{
	std::string script_path = ent->GetVirtualScriptPath();
	//unsigned int instance = 0;
	//get_entity_path_from_id_string( ent->GetInstancePath(), script_path, instance);
    {
        auto search = m_item_objs.find(script_path);
        if(search == m_item_objs.end()) {
            return false;
        }

        sol::environment env;
        std::string name;
        get_parent_env_of_entity(script_path, env, name);

		auto i = std::begin(search->second);

		while (i != std::end(search->second)) {
			if ((*i)->script_ent == ent )
			{
				std::shared_ptr<entity_wrapper> ew = *i;
				destroy_entity(ew);
				i = search->second.erase(i);
			}
			else
				++i;
		}
		/*
        for(auto e : search->second) {
			if( e->script_ent == ent )
			{
				 destroy_entity(e);
				 
			}
			//deregister_item(dynamic_cast<itemobj*>(e->script_ent));
        }
		*/
		if( search->second.size() == 0 )
		{
            std::stringstream ss;
            ss << "Destroyed environment " << script_path;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ss.str());
			env[name] = sol::nil;
		}
    }

    // lua.collect_garbage();

    return true;
}

bool entity_manager::destroy_npc(std::string& script_path)
{

    // sol::state& lua = (*m_state);
    {
        auto search = m_npc_objs.find(script_path);
        if(search == m_npc_objs.end()) {
            return false;
        }

        sol::environment env;
        std::string name;
        get_parent_env_of_entity(script_path, env, name);

        for(auto e : search->second) {
			//deregister_npc(dynamic_cast<npcobj*>(e->script_ent));
            destroy_entity(e);
        }

        env[name] = sol::nil;
        m_npc_objs.erase(search);
    }

    // lua.collect_garbage();

    return true;
}

bool entity_manager::destroy_npc(script_entity* ent)
{
	std::string script_path = ent->GetVirtualScriptPath();
	//std::string script_path="";
	//unsigned int instance = 0;
	//get_entity_path_from_id_string( ent->GetInstancePath(), script_path, instance);
    {
        auto search = m_npc_objs.find(script_path);
        if(search == m_npc_objs.end()) {
            return false;
        }

        sol::environment env;
        std::string name;
        get_parent_env_of_entity(script_path, env, name);

		auto i = std::begin(search->second);

		while (i != std::end(search->second)) {
			if ((*i)->script_ent == ent )
			{
				std::shared_ptr<entity_wrapper> ew = *i;
				destroy_entity(ew);
				i = search->second.erase(i);
			}
			else
				++i;
		}
		/*
        for(auto e : search->second) {
			if( e->script_ent == ent )
			{
				 destroy_entity(e);
				 
			}
			//deregister_item(dynamic_cast<itemobj*>(e->script_ent));
        }
		*/
		if( search->second.size() == 0 )
		{
            std::stringstream ss;
            ss << "Destroyed environment " << script_path;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ss.str());
			env[name] = sol::nil;
		
		}
    }

   // (*m_state).collect_garbage();

    return true;
}

bool entity_manager::destroy_daemon(script_entity* ent)
{
	std::string script_path = ent->GetVirtualScriptPath();
	//std::string script_path="";
	//unsigned int instance = 0;
	//get_entity_path_from_id_string( ent->GetInstancePath(), script_path, instance);
    {
        auto search = m_daemon_objs.find(script_path);
        if(search == m_daemon_objs.end()) {
            return false;
        }

        sol::environment env;
        std::string name;
        get_parent_env_of_entity(script_path, env, name);

		auto i = std::begin(search->second);

		while (i != std::end(search->second)) {
			if ((*i)->script_ent == ent )
			{
				std::shared_ptr<entity_wrapper> ew = *i;
				destroy_entity(ew);
				i = search->second.erase(i);
			}
			else
				++i;
		}

		if( search->second.size() == 0 )
		{
            std::stringstream ss;
            ss << "Destroyed environment " << script_path;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ss.str());
			env[name] = sol::nil;
		
		}
    }

    (*m_state).collect_garbage();

    return true;
}



bool entity_manager::destroy_daemon(std::string& script_path)
{

    // sol::state& lua = (*m_state);
    {
        auto search = m_daemon_objs.find(script_path);
        if(search == m_daemon_objs.end()) {
            return false;
        }

        sol::environment env;
        std::string name;
        get_parent_env_of_entity(script_path, env, name);

        for(auto e : search->second) {
			//deregister_daemon(dynamic_cast<daemonobj*>(e->script_ent));
            destroy_entity(e);
        }
        // sol::environment en_ = (*m_state).globals(); // env[name];
        env[name] = sol::nil;
        m_daemon_objs.erase(search);
    }

    // lua.collect_garbage();

    return true;
}

bool entity_manager::destroy_entity(std::shared_ptr<entity_wrapper>& ew)
{
	std::stringstream ss;
	ss << "Destroying script entity " << ew->script_ent->GetInstancePath();
	log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ew->script_ent->GetInstancePath(), ss.str());

	ew->script_ent->invoke_on_destroy();
    ew->script_ent->clear_props(); // = NULL;
    sol::environment genv = (*m_state).globals();
    genv.set_on(ew->_script_f_);
    ew.reset();
    //(*m_state).collect_garbage();
    return true;
}

bool entity_manager::_init_entity_env(std::string& script_path, EntityType etype, sol::environment& env)
{
    // std::string envstr = std::regex_replace(script_path, std::regex("\\" +
    // global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH)), "");  // remove the extra pathing stuff we don't
    // need

    sol::state& lua = (*m_state);

    std::vector<std::string> strs;
    boost::split(strs, script_path, boost::is_any_of("/"));

    sol::environment current_env = lua.globals();

    for(auto& en : strs) {
        sol::optional<sol::environment> maybe_env = current_env[en + "_env_"];

        if(maybe_env) {
            // it has already been initialized
            current_env = maybe_env.value(); // set it so we can go deeper..

        } else {
            // be careful with the next call, if the environment exists then
            // this function will destroy it.

            _init_lua_env_(current_env, lua.globals(), en + "_env_", current_env);
            // just for now, so we can sanity check where we are..
            current_env["_FS_PATH_"] = en + "_env_";
        }
    }

    current_env["_INTERNAL_SCRIPT_PATH_"] = script_path;
    std::string s;
    get_entity_str(etype, s);
    current_env["_INTERNAL_ENTITY_TYPE_"] = s;
    env = current_env;
    return true;
}

bool entity_manager::_init_lua_env_(sol::environment parent,
                                    sol::environment inherit,
                                    std::string new_child_env_name,
                                    sol::environment& new_child_env)
{
    sol::state& lua = (*m_state);
    sol::optional<sol::environment> test_env =
        parent[new_child_env_name]; // parent.get<sol::environment>( new_child_env_name );
    assert(!test_env);
    if(test_env) {

        parent[new_child_env_name] = sol::nil;
        lua.collect_garbage();
    }
    sol::environment tmp = sol::environment(lua, sol::create, lua.globals());
    parent[new_child_env_name] = tmp;
    // std::string force_weak = "setmetatable(" + new_child_env_name + ", {__mode = 'k'})";
    // lua.script( force_weak , parent);
    // __index = _G
    // tmp["__index"] = "_G";
    new_child_env = tmp;

    return true;
}

void entity_manager::invoke_heartbeat()
{
    _heartbeat.do_heartbeats();
}

void entity_manager::register_command(commandobj* cmd)
{
    std::string verb = cmd->GetCommand();
    std::string ptmp = GetPathFromFileLocation(cmd->GetPhysicalScriptPath());
    std::transform(verb.begin(), verb.end(), verb.begin(), ::tolower);
    m_cmds_map[ptmp][verb] = cmd;

    std::stringstream ss;
    ss << "Registered command, command = " << verb << ", script=" << cmd->GetVirtualScriptPath();
	log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, cmd->GetVirtualScriptPath(), ss.str());
}

void entity_manager::deregister_command(commandobj* cmd)
{
	assert(cmd);
	std::string room_path = cmd->GetInstancePath();

    auto search = m_cmd_objs.find(room_path);
    if(search != m_cmd_objs.end()) {
        m_cmd_objs.erase(search);
        std::stringstream ss;
        ss << "De-Registered command from lookup table, command = " << room_path;
        log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, room_path, ss.str());
    }

}



void entity_manager::register_room(roomobj* room)
{
    std::string room_path = room->GetInstancePath();

    m_room_lookup[room_path] = room;
    std::stringstream ss;
    ss << "Registered room into lookup table, room = " << room_path;
    
    log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, room_path, ss.str());
    
    //save_room_cache(); // may need to rethink this approach
}

void entity_manager::deregister_room(roomobj* room)
{
	assert(room);
	std::string room_path = room->GetInstancePath();
	//std::stringstream ss;
  //  ss << "De-registered room from lookup table, room = " << room_path;
  //  log->debug(ss.str());
	
	//room->set_destroy(true);
    //std::string room_path = room->GetInstancePath();
    auto search = m_room_lookup.find(room_path);
    if(search != m_room_lookup.end() && room == search->second) {
        /*
        //std::vector<script_entity*> found;
        for( auto& i : search->second->GetInventory() )
        {

            if( i->GetType() == EntityType::NPC )
            {
                
                i->SetEnvironment(NULL);
                //i->set_destroy(true);
                //found.push_back(i);
                
                deregister_npc(dynamic_cast<npcobj*>(i));
               // std::string t_path = i->GetInstancePath();
                //destroy_npc(t_path);
            }
            else if( i->GetType() == EntityType::ITEM )
            {
                i->SetEnvironment(NULL);
               // i->set_destroy(true);
              //  found.push_back(i);
                
                deregister_item(dynamic_cast<itemobj*>(i));
            }

        }
		
		*/
        //search->second->ClearInventory();
        m_room_lookup.erase(search);
        
       // garbage_collect();
        //sol::state& lua = (*m_state);
        //lua.collect_garbage();
        std::stringstream ss;
        ss << "De-Registered room from lookup table, room = " << room_path;
		log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, room_path, ss.str());
        // search->second.insert(ew);
    }
}

void entity_manager::register_item(itemobj* item)
{

    std::string item_path = item->GetInstancePath();
    m_item_lookup[item_path] = item;

    std::stringstream ss;
    ss << "Registered item into lookup table, item = " << item_path;
	log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, item_path, ss.str());
}


void entity_manager::deregister_item(itemobj* item)
{
	assert(item);
	std::string item_path = item->GetInstancePath();
    auto search = m_item_lookup.find(item_path);
    if(search != m_item_lookup.end()) {
        m_item_lookup.erase(search);
        
        std::stringstream ss;
        ss << "De-Registered item from lookup table, item = " << item_path;
		log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, item_path, ss.str());
    }
}

void entity_manager::register_npc(npcobj* npc)
{
    std::string npc_path = npc->GetInstancePath();
    m_npc_lookup[npc_path] = npc;
    

    std::stringstream ss;
    ss << "Registered npc into lookup table, npc = " << npc_path;
	log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, npc_path, ss.str());
}

void entity_manager::deregister_npc(npcobj* npc)
{
	assert(npc);
	std::string npc_path = npc->GetInstancePath();

    auto search = m_npc_lookup.find(npc_path);
    if(search != m_npc_lookup.end()) {
        m_npc_lookup.erase(search);
        std::stringstream ss;
        ss << "De-Registered npc from lookup table, npc = " << npc_path;
		log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, npc_path, ss.str());
    }
}

void entity_manager::register_daemon(daemonobj* daemon)
{
    std::string daemon_path = daemon->GetInstancePath();
    m_daemon_lookup[daemon_path] = daemon;

    std::stringstream ss;
    ss << "Registered daemon into lookup table, daemon = " << daemon_path;
	log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, daemon_path, ss.str());
}

void entity_manager::deregister_daemon(daemonobj* daemon)
{
    std::string daemon_path = daemon->GetInstancePath();
    auto search = m_daemon_lookup.find(daemon_path);
    if(search != m_daemon_lookup.end()) {
        m_daemon_lookup.erase(search);
 
        std::stringstream ss;
        ss << "De-Registered daemon from lookup table, daemon = " << daemon_path;
		log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, daemon_path, ss.str());
    }
}

void entity_manager::register_entity(script_entity* entityobj, std::string& sp, EntityType etype)
{

    std::shared_ptr<entity_wrapper> ew(new entity_wrapper);
    ew->entity_type = etype;
    // assert(_currently_loading_script.size() != 0);
    ew->script_path = sp;
    // entityobj->SetScriptPath(ew->script_path);
    ew->script_ent = entityobj;
    ew->_script_f_ = m_state_internal->_current_script_f_; // <-- work around to ensure all objects in env get destroyed

    m_state_internal->_last_registered_object_ = entityobj;

    sol::environment e_parent;
    std::string env_name;
    get_parent_env_of_entity(ew->script_path, e_parent, env_name);

    //  The idea behind the next few calls is to make sure that each entity gets it's own
    //  instance ID.  Each time a entity loads we look to the parent env to grab
    //  the intance ID, then we increment for the next entity.  Most of the time
    //  only 1 entity will be instantiated per script, so this will only be used
    //  when people get clever.  Primarily, this allows the entity manager
    //  to figure out which object is being referenced, e.g., during a destroy, so
    //  the other entities declared in the script remain untouched

    sol::optional<unsigned int> instance_id = e_parent[env_name]["_internal_instance_id_"];
    if(!instance_id) {
        e_parent[env_name]["_internal_instance_id_"] = 0;
        //  ew->instance_id = 0;
        entityobj->instanceID = 0;
    } else {
        // ew->instance_id = instance_id.value();
        int new_id = instance_id.value() + 1;
        e_parent[env_name]["_internal_instance_id_"] = new_id;
        entityobj->instanceID = new_id;
    }


    std::stringstream ss;

    switch(etype) {
    case EntityType::PLAYER: {
        // assert(_currently_loading_script.size() != 0);
        auto search = m_player_objs.find(sp);
        if(search != m_player_objs.end()) {

            // search->second.insert(ew);
        } else {
            m_player_objs.insert({ ew->script_path, ew }); // new_set ); //new_set );get_entity_str()
            std::string e_str;
            get_entity_str(ew->entity_type, e_str);

            ss << "Registered new entity, type = " << e_str << ", Path =" << ew->script_path;
			log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ew->script_path, ss.str());
        }
    } break;
    case EntityType::ROOM: {
        // get the current instance ID from the env..
        // assert(_currently_loading_script.size() != 0);
        auto search = m_room_objs.find(sp);
        if(search != m_room_objs.end()) {

            search->second.insert(ew);
        } else {
            std::set<std::shared_ptr<entity_wrapper> > new_set;
            new_set.insert(ew);
            m_room_objs.insert({ ew->script_path, new_set }); // new_set ); //new_set );get_entity_str()
            std::string e_str;
            get_entity_str(ew->entity_type, e_str);

            ss << "Registered new entity, type = " << e_str << ", Path =" << ew->script_path;
            db_interface::Instance().update_room_cache(ew->script_path, RoomState::RS_LIVE);
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ew->script_path, ss.str());
        }
    } break;
    case EntityType::COMMAND: {
        // get the current instance ID from the env..
        // commandobj * cmdobj = static_cast<commandobj*>(entityobj);
        // std::string cmd_noun = cmdobj->GetName();
        //  auto cmd_search = m_base_cmds.find(cmd_noun);
        /*
        if( cmd_search != m_base_cmds.end() )
        {
            m_base_cmds.erase(cmd_search); // may need to move this destruct code elsewhere
        }
        */
        // m_base_cmds[cmd_noun] = cmdobj;
        // assert(_currently_loading_script.size() != 0);
        auto search = m_cmd_objs.find(sp);
        if(search != m_cmd_objs.end()) {

            search->second.insert(ew);
        } else {
            std::set<std::shared_ptr<entity_wrapper> > new_set;
            new_set.insert(ew);
            m_cmd_objs.insert({ ew->script_path, new_set }); // new_set ); //new_set );get_entity_str()
            std::string e_str;
            get_entity_str(ew->entity_type, e_str);
            ss << "Registered new entity, type = " << e_str << ", Path =" << ew->script_path;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ew->script_path, ss.str());
        }
    } break;
    case EntityType::NPC: {
        auto search = m_npc_objs.find(sp);
        if(search != m_npc_objs.end()) {
            search->second.insert(ew);
        } else {
            std::set<std::shared_ptr<entity_wrapper> > new_set;
            new_set.insert(ew);
            m_npc_objs.insert({ ew->script_path, new_set }); // new_set ); //new_set );get_entity_str()
            std::string e_str;
            get_entity_str(ew->entity_type, e_str);
            ss << "Registered new entity, type = " << e_str << ", Path =" << ew->script_path;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ew->script_path, ss.str());
        }
    } break;
    case EntityType::ITEM: {
        auto search = m_item_objs.find(sp);
        if(search != m_item_objs.end()) {
            search->second.insert(ew);
        } else {
            std::set<std::shared_ptr<entity_wrapper> > new_set;
            new_set.insert(ew);
            m_item_objs.insert({ ew->script_path, new_set }); // new_set ); //new_set );get_entity_str()
            std::string e_str;
            get_entity_str(ew->entity_type, e_str);
            ss << "Registered new entity, type = " << e_str << ", Path =" << ew->script_path;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ew->script_path, ss.str());
        }
    } break;
    case EntityType::DAEMON: {
        // assert(_currently_loading_script.size() != 0);
        auto search = m_daemon_objs.find(sp);
        if(search != m_daemon_objs.end()) {

            search->second.insert(ew);
        } else {
            std::set<std::shared_ptr<entity_wrapper> > new_set;
            new_set.insert(ew);
            m_daemon_objs.insert({ ew->script_path, new_set }); // new_set ); //new_set );get_entity_str()
            std::string e_str;
            get_entity_str(ew->entity_type, e_str);
            ss << "Registered new entity, type = " << e_str << ", Path =" << ew->script_path;
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ew->script_path, ss.str());
        }
    } break;
    default:
        break;
    }
}



void entity_manager::get_rooms_from_path(std::string& script_path, std::set<std::shared_ptr<entity_wrapper> >& rooms)
{
    auto search = m_room_objs.find(script_path);
    if(search != m_room_objs.end()) {
        for(auto& e : search->second) {
            rooms.insert(e);
        }
    }
}

void entity_manager::get_npcs_from_path(std::string& script_path, std::set<std::shared_ptr<entity_wrapper> >& npcs)
{
    auto search = m_npc_objs.find(script_path);
    if(search != m_npc_objs.end()) {
        for(auto& e : search->second) {
            npcs.insert(e);
        }
    }
}

void entity_manager::get_items_from_path(std::string& script_path, std::set<std::shared_ptr<entity_wrapper> >& items)
{
    auto search = m_item_objs.find(script_path);
    if(search != m_item_objs.end()) {
        for(auto& e : search->second) {
            items.insert(e);
        }
    }
}

void entity_manager::get_daemons_from_path(std::string& script_path,
                                           std::set<std::shared_ptr<entity_wrapper> >& deamons)
{
    auto search = m_daemon_objs.find(script_path);
    if(search != m_daemon_objs.end()) {
        for(auto& e : search->second) {
            deamons.insert(e);
        }
    }
}

void entity_manager::get_cmds_from_path(std::string& script_path,
                                           std::set<std::shared_ptr<entity_wrapper> >& cmds)
{
    auto search = m_cmd_objs.find(script_path);
    if(search != m_cmd_objs.end()) {
        for(auto& e : search->second) {
            cmds.insert(e);
        }
    }
}

bool entity_manager::get_parent_env_of_entity(std::string& script_path, sol::environment& env, std::string& env_name)
{
    sol::state& lua = (*m_state);
    std::vector<std::string> strs;
    boost::split(strs, script_path, boost::is_any_of("/"));

    // the second to the last will always be the parent,
    // ex/ realms/void, realms is parent
    sol::environment curr_env = lua.globals();
    for(unsigned int x = 0; x < strs.size(); x++) {
        curr_env = curr_env[strs[x] + "_env_"];
        if(x == strs.size() - 2) {
            env = curr_env;
            env_name = strs[x + 1] + "_env_";
            return true;
        }
    }
    return false;
}

void entity_manager::reset()
{
    this->m_daemon_lookup.clear();
    this->m_cmds_map.clear();
    this->m_room_lookup.clear();
    this->m_item_lookup.clear();
    this->m_npc_lookup.clear();
    
    

    _heartbeat.deregister_all_heartbeat_funcs();
    
    std::vector<std::string> destroy_entities; 
    for(auto& cmd : m_npc_objs) {
        for(auto& ew : cmd.second) {
            destroy_entities.push_back(ew->script_path);
            //m_entity_cleanup.push_back(ew->script_ent);
			//destroy_npc(ew->script_ent);
        }
    }
	for(auto& npcname : destroy_entities)
        destroy_npc(npcname);
   // garbage_collect();
   // this->m_state_internal.reset();
   // m_state.reset();
    //for(auto& npcname : destroy_entities)
    //    destroy_npc(npcname);
    //    
    destroy_entities.clear();

    
    for(auto& e : m_room_objs) {
        for(auto& ew : e.second) {
            destroy_entities.push_back(ew->script_path);
            //m_entity_cleanup.push_back(ew->script_ent);
        }
    }
    
    //garbage_collect();
    for(auto& rname : destroy_entities)
        destroy_room(rname);

    destroy_entities.clear();

    for(auto& e : m_player_objs) {
         playerobj * pe = dynamic_cast<playerobj*>(e.second->script_ent);
         pe->disconnect_client();
        // destroy_entities.push_back(e.second->script_path);
        //m_entity_cleanup.push_back(e.second->script_ent);
    }

    for(auto& pname : destroy_entities)
        destroy_player(pname);

    destroy_entities.clear();
	
    for(auto& d : m_daemon_objs) {
        for(auto& ew : d.second) {
            destroy_entities.push_back(ew->script_path);
         //   m_entity_cleanup.push_back(ew->script_ent);
        }
    }

    for(auto& dname : destroy_entities)
        destroy_daemon(dname);
    //garbage_collect();

    destroy_entities.clear();
	
    for(auto& cmd : m_cmd_objs) {
        for(auto& ew : cmd.second) {
            destroy_entities.push_back(ew->script_path);
            //m_entity_cleanup.push_back(ew->script_ent);
        }
    }

    for(auto& cmdname : destroy_entities)
        destroy_command(cmdname);

    destroy_entities.clear();
    
	for(auto& cmd : m_item_objs) {
        for(auto& ew : cmd.second) {
            destroy_entities.push_back(ew->script_path);
            //m_entity_cleanup.push_back(ew->script_ent);
        }
    }
    garbage_collect();

    for(auto& itemname : destroy_entities)
        destroy_item(itemname);

    this->m_state_internal.reset();
    m_state.reset();
    // m_room_objs.clear();
}

void entity_manager::garbage_collect()
{
    sol::state& lua = (*m_state);
    lua.collect_garbage();
	return;
	/*
    // this->m_daemon_lookup.clear();
    // this->m_default_cmds.clear();
    // this->m_room_lookup.clear();
    // this->m_item_lookup.clear();

    //_heartbeat.deregister_all_heartbeat_funcs();
    
    for( script_entity * i : m_entity_cleanup )
    {
		if( i == NULL ) // handle a case where lua destroyed object before us..
			continue;
        std::string entity_script_path = i->GetVirtualScriptPath();
		//i->set_destroy(true);
        switch( i->GetType() )
        {
            case EntityType::ITEM:
            {
                //deregister_item( static_cast<itemobj*>(i));
                destroy_item( entity_script_path);
            }break;
            case EntityType::NPC:
            {
                //deregister_npc( static_cast<npcobj*>(i));
                destroy_npc( entity_script_path );
            }break;
            case EntityType::COMMAND:
            {
                //deregister_command( static_cast<commandobj*>(i) );
                destroy_command( entity_script_path );               
            }break;
            case EntityType::DAEMON:
            {
                //deregister_daemon( static_cast<daemonobj*>(i) );
                destroy_daemon( entity_script_path );     
            }break;
            case EntityType::PLAYER:
            {
               // deregister_player( dynamic_cast<playerobj*>(i) );
              //  destroy_player( entity_script_path);                   
            }break;
            case EntityType::ROOM:
            {
               // deregister_room( static_cast<roomobj*>(i) );
                destroy_room( entity_script_path );   
            }break;
        }
    }
    
    m_entity_cleanup.clear();
	//m_npc_lookup.clear();
    */
    /*

    std::vector<std::string> destroy_entities;
    for(auto& e : m_room_objs) {
        for(auto& ew : e.second) {
            if(ew->script_ent->get_destroy()) {
                destroy_entities.push_back(ew->script_path);
                deregister_room(static_cast<roomobj*>(ew->script_ent));
            }
        }
    }

    for(auto& rname : destroy_entities)
        destroy_room(rname);

    destroy_entities.clear();

    for(auto& e : m_player_objs) {
        if(e.second->script_ent->get_destroy()) {
            destroy_entities.push_back(e.second->script_path);
            //( static_cast<daemonobj*>(ew->script_ent) );
        }
    }

    for(auto& pname : destroy_entities)
        destroy_player(pname);

    destroy_entities.clear();
    for(auto& d : m_daemon_objs) {
        for(auto& ew : d.second) {
            if(ew->script_ent->get_destroy()) {
                destroy_entities.push_back(ew->script_path);
                deregister_daemon(static_cast<daemonobj*>(ew->script_ent));
            }
        }
    }

    for(auto& dname : destroy_entities)
        destroy_daemon(dname);

    destroy_entities.clear();
    for(auto& cmd : m_cmd_objs) {
        for(auto& ew : cmd.second) {
            if(ew->script_ent->get_destroy()) {
                destroy_entities.push_back(ew->script_path);
                deregister_command(static_cast<commandobj*>(ew->script_ent));
            }
        }
    }

    for(auto& cmdname : destroy_entities)
        destroy_command(cmdname);

    destroy_entities.clear();
    for(auto& cmd : m_item_objs) {
        for(auto& ew : cmd.second) {
            if(ew->script_ent->get_destroy()) {
                destroy_entities.push_back(ew->script_path);
                deregister_item(static_cast<itemobj*>(ew->script_ent));
            }
        }
    }

    for(auto& itemname : destroy_entities) {
        destroy_item(itemname);
        // this->m_item_lookup.clear();
    }
    


    destroy_entities.clear();
    for(auto& cmd : m_npc_objs) {
        for(auto& ew : cmd.second) {
            if(ew->script_ent->get_destroy()) {
                destroy_entities.push_back(ew->script_path);
                deregister_item(static_cast<itemobj*>(ew->script_ent));
            }
        }
    }

    for(auto& npcname : destroy_entities) {
        destroy_npc(npcname);
        // this->m_item_lookup.clear();
    }
    */

    // this->m_state_internal.reset();
    // m_state.reset();
    // m_room_objs.clear();
}

roomobj* entity_manager::GetRoomByScriptPath(std::string& script_path, unsigned int instance_id)
{
    if(script_path.empty())
        return NULL;

    std::string room_path = script_path + ":id=" + std::to_string(instance_id);
    if(room_path[0] == '/') {
        room_path = room_path.erase(0, 1);
    }
    auto search = m_room_lookup.find(room_path);
    if(search != m_room_lookup.end()) {
        return search->second;
    }
    /*
    std::set<std::shared_ptr<entity_wrapper> > rooms;
    get_rooms_from_path(script_path, rooms);

    for(auto& ew : rooms) {
        if(ew->instance_id == instance_id) {
            return dynamic_cast<roomobj*>(ew->script_ent);
        }
    }
    */

    return NULL;
}
npcobj* entity_manager::GetNPCByScriptPath(std::string& script_path, unsigned int instance_id)
{
    std::string item_path = script_path + ":id=" + std::to_string(instance_id);
    auto search = m_npc_lookup.find(script_path);
    if(search != m_npc_lookup.end()) {
        return search->second;
    }
    return NULL;
}

npcobj* entity_manager::GetNPCByScriptPath(std::string& script_path)
{
    // std::string room_path = script_path + ":id=" + std::to_string(instance_id);
    std::string itempath = script_path;
    std::size_t found = itempath.find(":id=");
    if(found == std::string::npos) {
        itempath += ":id=0";
    }
    auto search = m_npc_lookup.find(itempath);
    if(search != m_npc_lookup.end()) {
        return search->second;
    }

    return NULL;
}

itemobj* entity_manager::GetItemByScriptPath(std::string& script_path, unsigned int instance_id)
{
    std::string item_path = script_path + ":id=" + std::to_string(instance_id);
    auto search = m_item_lookup.find(script_path);
    if(search != m_item_lookup.end()) {
        return search->second;
    }
    /*
    std::set<std::shared_ptr<entity_wrapper> > rooms;
    get_rooms_from_path(script_path, rooms);

    for(auto& ew : rooms) {
        if(ew->instance_id == instance_id) {
            return dynamic_cast<roomobj*>(ew->script_ent);
        }
    }
    */

    return NULL;
}

itemobj* entity_manager::GetItemByScriptPath(std::string& script_path)
{
    // std::string room_path = script_path + ":id=" + std::to_string(instance_id);
    std::string itempath = script_path;
    std::size_t found = itempath.find(":id=");
    if(found == std::string::npos) {
        itempath += ":id=0";
    }
    auto search = m_item_lookup.find(itempath);
    if(search != m_item_lookup.end()) {
        return search->second;
    }

    return NULL;
}

roomobj* entity_manager::GetRoomByScriptPath(std::string& script_path)
{
    // std::string room_path = script_path + ":id=" + std::to_string(instance_id);
    if(script_path.empty())
        return NULL;

    std::string roompath = script_path;

    if(roompath[0] == '/') {
        roompath = roompath.erase(0, 1);
    }

    std::size_t found = roompath.find(":id=");
    if(found == std::string::npos) {
        roompath += ":id=0";
    }

    auto search = m_room_lookup.find(roompath);
    if(search != m_room_lookup.end()) {
        return search->second;
    }
    /*
    std::set<std::shared_ptr<entity_wrapper> > rooms;
    get_rooms_from_path(script_path, rooms);

    for(auto& ew : rooms) {
        if(ew->instance_id == instance_id) {
            return dynamic_cast<roomobj*>(ew->script_ent);
        }
    }
    */

    return NULL;
}

daemonobj* entity_manager::GetDaemonByScriptPath(std::string& script_path, unsigned int& instance_id)
{
    std::string daemon_path = script_path + ":id=" + std::to_string(instance_id);
    auto search = m_daemon_lookup.find(daemon_path);
    if(search != m_daemon_lookup.end()) {
        return search->second;
    }
    /*
    std::set<std::shared_ptr<entity_wrapper> > dms;
    get_daemons_from_path(script_path, dms);

    for(auto& ew : dms) {
        if(ew->instance_id == instance_id) {
            return dynamic_cast<daemonobj*>(ew->script_ent);
        }
    }
    */
    return NULL;
}

playerobj* entity_manager::get_player(const std::string& player_name)
{
    std::string player_script_path = global_settings::Instance().GetSetting(BASE_PLAYER_ENTITY_PATH);
    player_script_path += "_" + boost::to_lower_copy(player_name);

    // file_path, std::regex("\\" + global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH)), "");
    //_currently_loading_script = script_;

    auto search = m_player_objs.find(player_script_path);
    if(search != m_player_objs.end()) {
        return static_cast<playerobj*>(search->second->script_ent);
    } else {
        return NULL;
    }
}


bool entity_manager::move_living(script_entity* target, const std::string& roomid)
{
    if(roomid.size() == 0) {
        target->debug("Error, path cannot be empty.");
        return false;
    }
    std::string roompath = boost::to_lower_copy(roomid);
    std::size_t found = roompath.find(":id=");
    if(found == std::string::npos) {
        roompath += ":id=0";
    }
    if(roompath[0] == '/')
        roompath.erase(0, 1);

    roomobj* r = GetRoomByScriptPath(roompath);
    if(r != NULL) {
        move_entity(target, static_cast<script_entity*>(r));
    } else {
        std::stringstream ss;
        ss << "Error. Path [" << roomid << "] not found.";
        target->debug(ss.str());
        return false;
    }
    return true;
}

bool entity_manager::move_entity(script_entity* target, script_entity* dest)
{
    assert(target != NULL);
    assert(dest != NULL);
    // bunch of sanity checks.. which should be done on the lua side but will
    // be enforced here too.
    switch(target->GetType()) {
    case EntityType::ROOM:
    case EntityType::DAEMON:
    case EntityType::UNKNOWN: {
        return false; // can't move these..
        break;
    }
    case EntityType::ITEM: {
        // TODO: Implement item movement
        break;
    }
    case EntityType::PLAYER:
    case EntityType::NPC: {
        if(dest->GetType() != EntityType::ROOM) {
            return false;
        }
        if(roomobj* r = dynamic_cast<roomobj*>(dest)) {
            living_entity* p = dynamic_cast<living_entity*>(target);
            if(p->GetEnvironment() != NULL)
                p->GetRoom()->RemoveEntityFromInventory(target);
            r->AddEntityToInventory(target);
            return true;
        } else {
            // cannot move into a non-container..
            return false;
        }
        break;
    }
    default:
        return false;
        break;
    }
    return true;
}

void entity_manager::debug(std::string& msg)
{
    // LOG_DEBUG << msg;
}

bool entity_manager::do_command(living_entity* e, const std::string cmd, bool antiFlood)
{

    if(antiFlood) {
        if(auto pp = dynamic_cast<playerobj*>(e)) {
            // here is the pattern:

            // check it see if 1s has passed since the last tick, this essentially
            // allows for more commands to be received.  If we are in a new command window time
            // then accept N commands up to the max commands

            pt::ptime t2 = pt::second_clock::local_time();
            pt::time_duration diff = t2 - pp->get_referenceTickCount();
            if(diff.total_milliseconds() <= 1000) {
                // we're within a window.. check for command count..
                if(pp->get_commandCount() == pp->get_maxCmdCount()) {
                    pp->SendToEntity(
                        "You have exceeded the maximum number of type-ahead commands... please wait..\r\n");
                    // pp->set_referenceTickCount(); // anti flood protection.  Every time we hit this condition we
                    // continue to block more commands.
                    return false;
                }
                pp->incrementCmdCount();
            } else {
                // reset the window..
                pp->set_referenceTickCount();
                pp->resetCmdCount();
            }
        }
    }

    // e->SendToEntity("OK..");
    std::unique_lock<std::mutex> lock(dispatch_queue_mutex_);
    _internal_queue_wrapper_ iqw;
    iqw.f = std::bind(&entity_manager::on_cmd, this, e, cmd);
    iqw.ent = e;
    dispatch_queue_.push_back(iqw);
	assert(strand_);
    (*strand_).post(std::bind(&entity_manager::dispatch_queue, this));
    // on_cmd(e, cmd);

    return true;
}

void entity_manager::debug_script(script_entity* se, const std::string& msg)
{
    switch(se->GetType()) {
    case EntityType::ROOM: {
        // LOG_DEBUG_(RoomLog) << "[" << se->GetInstancePath() << "] " << msg;
    } break;
    default:
        break;
    }
}

std::vector<std::string> entity_manager::tail_entity_log(script_entity* se)
{
    // for(;;) {
    // std::ifstream infile("filename");
    // int current_position = find_new_text(infile);
    // sleep(1);
    // }
    std::vector<std::string> s;
    return s;
}

std::string entity_manager::do_translate_path(std::string& entitypath, playerobj* p)
{
    std::string temp = entitypath;
    std::string reason;
    if(!fs_manager::Instance().translate_path(temp, p, reason)) {
        p->SendToEntity(reason);
        return "";
    }
    temp =
        std::regex_replace(temp, std::regex("\\" + global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH)), "");
    return temp;
}

bool entity_manager::do_update(std::string& entitypath, playerobj* p)
{
    std::string temp = entitypath;
    std::string reason;
    if(!fs_manager::Instance().translate_path(temp, p, reason)) {
        p->SendToEntity(reason);
        return false;
    }
    if(fs::is_directory(temp)) {
        p->SendToEntity("Directory compiling not yet implemeneted..");
        return false;
    }

    // std::string reason;
    if(!compile_script_file(temp, reason)) {
        //p->SendToEntity(std::string("Error: ") + reason);
        return false;
    }

    return true;
}

bool entity_manager::do_goto(std::string& entitypath, playerobj* p)
{
    std::string temp = entitypath;

    std::string reason;
    if(!fs_manager::Instance().translate_path(temp, p, reason)) {
        p->SendToEntity(reason);
        return false;
    }
    if(p == NULL) {
        p->SendToEntity("Target player cannot be null");
    }

    std::string rel_path =
        std::regex_replace(temp, std::regex(global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH)), "");

    roomobj* r = GetRoomByScriptPath(rel_path);
    if(r != NULL) {
        return move_living(p, r->GetInstancePath());
    } else {
        p->SendToEntity("Unable to goto location, room does not exist.");
    }

    return false;
}

bool entity_manager::do_tp(std::string& entitypath, playerobj* p_targ, playerobj* p_caller)
{
    std::string temp = entitypath;

    std::string reason;
    if(!fs_manager::Instance().translate_path(temp, p_caller, reason)) {
        p_caller->SendToEntity(reason);
    }
    if(p_targ == NULL || p_caller == NULL) {
        p_caller->SendToEntity("Target player cannot be null");
    }

    std::string rel_path =
        std::regex_replace(temp, std::regex(global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH)), "");

    roomobj* r = GetRoomByScriptPath(rel_path);
    if(r != NULL) {
        return move_living(p_targ, r->GetInstancePath());
    } else {
        p_caller->SendToEntity("Unable to goto location, room does not exist.");
    }

    return false;
}

bool entity_manager::do_promote(playerobj* a, std::string b)
{
    if(a->get_accountType() != AccountType::ARCH) {
        a->SendToEntity("You lack the necessary privileges to promote a player.");
        return false;
    }

    account tmp;
    if(account_manager::Instance().load_account(b, tmp)) {
        if(tmp._accountType == AccountType::ARCH) {
            a->SendToEntity("Target player cannot be promoted, they are already an arch creator.");
            return false;
        }

        std::stringstream ss;
        ss << "Promoting " << b << " from " << account::AccountTypeToString(tmp._accountType) << " to "
           << account::AccountTypeToString((AccountType)(tmp._accountType + 1)) << ".";
        a->SendToEntity(ss.str());
        tmp._accountType = (AccountType)(tmp._accountType + 1);
        tmp.do_save();

        // Here we check to see if the player was promoted to a creator, and if so, to create their workspace + base set
        // of files such as their workroom.
        // My current thought is to have a directory full of templated files that just get copied in, including
        // directories
        if(tmp._accountType == AccountType::CREATOR || tmp._accountType == AccountType::ARCH) {
            std::string reason;
            std::string new_workspace_path;
            if(!fs_manager::Instance().do_create_new_workspace(b, new_workspace_path, reason)) {
                std::stringstream ss;
                ss << "Error creating workspace for " << b << ", reason = " << reason << ".";
                a->SendToEntity(ss.str());
            } else {
                std::stringstream ss;
                ss << "Created workspace directory for for " << b << ".";
                a->SendToEntity(ss.str());

                tmp._workspacePath = new_workspace_path;
                tmp._workspacePath = std::regex_replace(
                    tmp._workspacePath,
                    std::regex("\\" + global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH)),
                    ""); // remove the extra pathing stuff we don't
                tmp.do_save();

                // Now compile their workroom...
                std::string res = "";
                std::string wpath = new_workspace_path + "/workroom";
                if(!compile_script_file(wpath, res)) {
                    std::stringstream ss;
                    ss << "Error compiling workroom for " << b << ", reason = " << res << ".";
                    a->SendToEntity(ss.str());
                }
            }
        }

        return true;
    }
    return false;
}

void entity_manager::invoke_actions()
{
	pt::ptime t2 = pt::second_clock::local_time();
    pt::time_duration diff = t2 - garbage_tick_;
	if(diff.total_milliseconds() / 1000 > 15) {
        garbage_tick_ = t2;
		//garbage_collect();
    }

    for(auto r : this->m_room_lookup) {
        security_context::Instance().SetCurrentEntity(r.second);
        r.second->DoActions();
    }
    for(auto r : this->m_npc_lookup) {
        security_context::Instance().SetCurrentEntity(r.second);
        r.second->DoActions();
    }
    for(auto r : this->m_item_lookup) {
        security_context::Instance().SetCurrentEntity(r.second);
        r.second->DoActions();
    }
    for(auto r : this->m_daemon_lookup) {
        security_context::Instance().SetCurrentEntity(r.second);
        r.second->DoActions();
    }
}

bool entity_manager::capture_input(sol::this_state ts, playerobj* p, sol::object f)
{
    sol::state_view lua_state = sol::state_view(ts);
    sol::protected_function pf(f);
    sol::environment target_env(sol::env_key, pf);
    sol::optional<std::string> script_path = target_env["_INTERNAL_SCRIPT_PATH_"];
    // sol::optional<std::string> script_path = target_env["_INTERNAL_PHYSICAL_SCRIPT_PATH_"];
    // sol::optional<std::string> e_type = target_env["_INTERNAL_ENTITY_TYPE_"];

    if(script_path) {

        return true;
    } else {
        return false;
    }
}

bool entity_manager::load_room_cache(std::vector<std::string>& cache, std::string& reason)
{
    try {
        json _j;
        auto game_root_path =
            boost::filesystem::canonical(global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH));
        auto room_cache = global_settings::Instance().GetSetting(DEFAULT_ROOM_CACHE_PATH);

        auto patha = boost::filesystem::weakly_canonical(game_root_path / room_cache);

        std::ifstream i(patha.string() + "/roomcache");
        i >> _j;

        assert(!_j["room_cache"].is_null());
        if(!_j["room_cache"].is_null()) {
            cache = _j["room_cache"].get<std::vector<std::string> >();
        }

    } catch(std::exception& ex) {
        std::stringstream ss;
        ss << "Error when attempting to load account " << ex.what() << ".";
        log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ss.str());
        reason = ss.str();
        return false;
    }
    return true;
}

template <typename T>
void remove_duplicates(std::vector<T>& vec)
{
  sort(vec.begin(), vec.end());
  vec.erase(unique(vec.begin(), vec.end()), vec.end());
}

bool entity_manager::save_room_cache()
{
    // TODO:  make this a lazy-write system, as more rooms are added this disk access could become very expensive
    // std::map< std::string, roomobj * > m_room_lookup;
	
    std::vector<std::string> tmp;
    for(auto& k : m_room_objs) {
        std::cout << "Adding " << k.first << " to the list.." << std::endl;
        tmp.push_back(k.first);
    }
	
	std::vector<std::string> old_cache; // make sure we save all rooms including those that failed to compile
	std::string reason;
	if( load_room_cache( old_cache, reason ) )
	{
		for( auto rtmp : old_cache )
		{
			tmp.push_back(rtmp);
		}
	}
	
	remove_duplicates(tmp);

    json j;

    try {

        auto game_root_path =
            boost::filesystem::canonical(global_settings::Instance().GetSetting(DEFAULT_GAME_DATA_PATH));
        auto room_cache = global_settings::Instance().GetSetting(DEFAULT_ROOM_CACHE_PATH);

        auto patha = boost::filesystem::weakly_canonical(game_root_path / room_cache);

        j["room_cache"] = tmp;

        std::ofstream o(patha.string() + "/roomcache");
        o << std::setw(4) << j << std::endl;
    } catch(std::exception& ex) {
        std::stringstream ss;
        ss << "Error when attempting to save room cache "
           << ": " << ex.what() << ".";
        log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ss.str());
        return false;
    }

    return true;
}

bool binit = false;

/**
 *  Only call the following method from the dispather function to ensure
 * the lua state remains uncorrupted
 */
void entity_manager::on_cmd(living_entity* e, std::string const& cmd)
{
    if(e == NULL)
        return;

    try {
        std::unique_lock<std::recursive_mutex> lock(lua_mutex_);

        bool bdoLogon = false;
		
        auto pp = dynamic_cast<playerobj*>(e);

        if(pp) {
            if(!pp->get_loggedIn()) {
                bdoLogon = true;
            }
        }

        if(bdoLogon) {
            // std::unique_lock<std::mutex> lock(lua_mutex_);
            std::string logon_proc_path = global_settings::Instance().GetSetting(DEFAULT_LOGON_PROC);
            unsigned int id = 0;
            daemonobj* dobj = entity_manager::Instance().GetDaemonByScriptPath(logon_proc_path, id);
            if(dobj == NULL) {
                std::stringstream ss;
                ss << "Error attempting to retrieve command proc.";
                log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ss.str());

                return;
            }

            sol::optional<sol::table> self = dobj->m_userdata->selfobj;
            if(self) {
                sol::protected_function exec = self.value()["process_command"];
                auto result = exec(self, e, cmd);

                if(result.valid()) {
                    bool bOk = true;
                    bool bAuth = true;

                    sol::tie(bOk, bAuth) = result;

                    if(!bOk) {
                        pp->disconnect_client();
                    } else if(bAuth) {
                        pp->get_client()->associate_player(pp->GetPlayerName(), true);
                        pp->set_client(NULL);
                        playerobj* po = get_player(pp->GetPlayerName());
                        po->set_loggedIn(true);
                        std::string spath = pp->GetVirtualScriptPath();
                        destroy_player(spath);
                        //garbage_collect();

                        std::stringstream ss;
                        
                        ss << "Player " << pp->GetPlayerName() << " logged in successfully!";
						//ss << "Player IP " << po->get_client()->get_ip_address();
						std::string str = po->get_client()->get_ip_address();
						log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ss.str());
						db_interface::Instance().on_player_connection_event( po->GetPlayerName(), ConnectionEvent::PLAYER_LOGON, str );
                        
                        // e->debug(ss.str());
                        // lock.unlock();
                        po->DoCommand(NULL, "look");
                    }
                } else {
                    sol::error err = result;
                    
                    std::stringstream ss;
                    ss << "Error calling process_command, entity = " << e->GetName() << ", error = " << err.what();
                    log_interface::Instance().log(LOGLEVEL::LOGLEVEL_ERROR, ss.str());
                    e->debug(ss.str());
                }
                return;
            } else {
                std::stringstream ss;
                ss << "Error attempting to retrieve command proc.";
                log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ss.str());
                return;
            }
        }

        {
             std::unique_lock<std::recursive_mutex> lock(lua_mutex_);

            security_context::Instance().SetCurrentEntity(e);

            std::string command_proc_path = global_settings::Instance().GetSetting(DEFAULT_COMMAND_PROC);
            unsigned int id = 0;
            daemonobj* dobj = entity_manager::Instance().GetDaemonByScriptPath(command_proc_path, id);
            if(dobj == NULL) {
                
                std::stringstream ss;
                ss << "Error attempting to retrieve command proc.";
                log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ss.str());
                return;
            }

            sol::optional<sol::table> self = dobj->m_userdata->selfobj;

            if(self) {
                sol::protected_function exec = self.value()["process_command"];
                // Regsiter anti-infinite loop detector
                register_hook(e);
                auto result = exec(self, e, cmd);
                if(!result.valid()) {
                    sol::error err = result;

                    std::stringstream ss;
                    ss << "Error calling process_command, entity = " << e->GetName() << ", error = " << err.what();
                    log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ss.str());
                    e->debug(ss.str());
                    register_hook(NULL); // clear the hook
                } else {
                    /*
                     * Prompt, todo: rethink this entirely
                    */

                    if(auto pp = dynamic_cast<playerobj*>(e)) {
                        if(!pp->isCreator()) {
                            e->SendToEntity("\r\n>");
                        } else {
                            std::stringstream ss;
                            ss << pp->cwd << ">";
                            e->SendToEntity(ss.str());
                        }
                    }

                    register_hook(NULL); // clear the hook
                }
                return;
            } else {
                //auto log = spd::get("main");
                std::stringstream ss;
                ss << "Error attempting to retrieve command proc.";
                log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ss.str());
                return;
            }
        }

    } catch(std::exception& ex) {
        std::stringstream ss;
        ss << "Error attempting to execute command proc " << ex.what();
        log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ss.str());
    }
}

void entity_manager::register_hook(script_entity* hook_entity)
{
    if(hook_entity == NULL) {
        this->m_state->script("debug.sethook ()");
        return;
    }

    try {

        this->m_state->set_function("debug_hook", [this, hook_entity](sol::object o) -> void {
            // sol::optional<std::string> spath = kv.second.script_path; // lua_state["_INTERNAL_SCRIPT_PATH_"];
            //  sol::optional<std::string> sent = kv.second.etype; // lua_state["_INTERNAL_ENTITY_TYPE_"];
            //  script_entity* se = NULL;
            //  if(sent && boost::to_lower_copy(sent.value()) == "room") {
            //      roomobj* re = entity_manager::Instance().GetRoomByScriptPath(spath.value(), 0);
            //      se = re;
            // }

            sol::optional<std::string> err = o.as<std::string>();
            if(err && err.value() == "count") {
                std::string s = "Infinite loop detected. Operation terminated.";
                if(hook_entity) {
                    // sol::this_state sta;
                    /// sta.L = kv.second.lua_state.lua_state();
                    // hook_entity->debug(s);
                }
                lua_pushstring(m_state->lua_state(), s.c_str());
                lua_error(m_state->lua_state());
            } else {
                std::string s = "Error. Reason = " + err.value();
                if(hook_entity) {
                    // hook_entity->debug(s);
                }
                lua_pushstring(m_state->lua_state(), s.c_str());
                lua_error(m_state->lua_state());
            }

        });

        m_state->script("debug.sethook (debug_hook, '', 100000)");


    } catch(std::exception& ex) {
        std::stringstream ss;
        ss << "Error setting debug hook: " << ex.what();
		log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ss.str());
    }
}

void entity_manager::bind_io(ba::io_service* ioservice)
{
    io_serv = ioservice;
    strand_ = boost::shared_ptr<boost::asio::strand>(new boost::asio::strand(*ioservice));
}

heartbeat_manager& entity_manager::get_heartbeat_manager()
{
    return _heartbeat;
}

void entity_manager::dispatch_queue()
{
    std::function<void()> fn;

    std::unique_lock<std::mutex> lock(dispatch_queue_mutex_);

    while(!dispatch_queue_.empty()) {
        fn = dispatch_queue_.front().f;
        dispatch_queue_.pop_front();
        lock.unlock();
        //std::unique_lock<std::recursive_mutex> lock(lua_mutex_);
        fn();
        lock.lock();
    }
}

// This function gets called by the destruction of each script element.
// The intent here is to try and sync the lookup tables with lua tables
void entity_manager::deregister_entity(script_entity* ent, bool bGarbageCollect)
{
	//return;
	std::unique_lock<std::recursive_mutex> lock(lua_mutex_);
	std::stringstream ss;
	ss << "De-registering entity: " << ent->GetInstancePath();
	log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ent->GetInstancePath(), ss.str());
	
	switch(ent->GetType())
	{
		case EntityType::COMMAND:
		{
			commandobj * cmd = static_cast<commandobj*>(ent);
			deregister_command(cmd);
			//m_entity_cleanup.push_back(ent);	
		}
		break;
		case EntityType::DAEMON:
		{
			daemonobj * d = static_cast<daemonobj*>(ent);
			deregister_daemon(d);
			//m_entity_cleanup.push_back(ent);		
		}
		break;
		case EntityType::HAND:
		{
			// gets destroyed with the npc/player.. no need to do anything here.
		}
		break;
		case EntityType::ITEM:
		{
			itemobj * i = static_cast<itemobj*>(ent);
			deregister_item(i);
			//destroy_item(i);
			// the following would have caused trouble in the living entity recursive unload routine..
			// need to rethink how to remove the item from its environment on deletion.
			// perhaps have a room that is a garbage heap that gets periodically burned??
			//if( i->GetEnvironment() )
			//{
			//	dynamic_cast<container_base*>(i->GetEnvironment())->RemoveEntityFromInventory(ent);
			//}
			//deregister_item(i);
			//m_entity_cleanup.push_back(ent);
			
		}
		break;
		case EntityType::LIB:
		{
			// nothing to do..
		}
		break;
		case EntityType::NPC:
		{
			npcobj * npc = static_cast<npcobj*>(ent);
			unload_npc(npc);
			deregister_npc(npc);
			//destroy_npc(npc);
			//npcobj * npc = static_cast<npcobj*>(ent);

			//deregister_npc(npc);
			//m_entity_cleanup.push_back(ent);		
		}
		break;
		case EntityType::PLAYER:
		{
			playerobj * po = static_cast<playerobj*>(ent);
			if( po->get_loggedIn() )
			{
				std::stringstream ss;

				ss << "Player " << po->GetPlayerName() << " logged off!";
				std::string str = po->get_client()->get_ip_address();
				log_interface::Instance().log(LOGLEVEL::LOGLEVEL_DEBUG, ss.str());
				db_interface::Instance().on_player_connection_event( po->GetPlayerName(), ConnectionEvent::PLAYER_LOGOFF, str );	
			}

		}
		break;
		case EntityType::ROOM:
		{
			roomobj * r = static_cast<roomobj*>(ent);
			deregister_room(r);
			//m_entity_cleanup.push_back(ent);	
		}
		break;
		case EntityType::UNKNOWN:
		{
            std::stringstream ss;
            ss << "Error scheduling deletion of object, type is unknown. " << ent->GetInstancePath();
            log_interface::Instance().log(LOGLEVEL::LOGLEVEL_CRITICAL, ent->GetInstancePath(), ss.str());
		}
		break;
	}
	if( bGarbageCollect)
	{
		garbage_collect();
	}
}
/*
bool entity_manager::compile_lib(std::string& script_or_path, std::string& reason)
{

    auto simple_handler = [](lua_State*, sol::protected_function_result result) {
        // You can just pass it through to let the call-site handle it
        return result;
    };

    if(!m_state) {
        m_state = std::shared_ptr<sol::state>(new sol::state);
        init_lua();
    }

     sol::state& lua = (*m_state);

    if( fs::exists(script_or_path) )
    {
        auto result = lua.script_file(script_or_path, simple_handler);
        if(result.valid()) {
            LOG_DEBUG << "Successfully executed script.";
            return true;
        } else {
            sol::error err = result;
            LOG_ERROR << err.what();
            return false;
        }
    }
    else
    {
        auto result = lua.script(script_or_path, simple_handler);
        if(result.valid()) {
            LOG_DEBUG << "Successfully executed script.";
            return true;
        } else {
            sol::error err = result;
            LOG_ERROR << err.what();
            return false;
        }
    }

}

*/
/*
bool entity_manager::get_command(std::string& verb, shared_ptr<entity_wrapper>& cmd)
{
    auto search = command_objs.find(boost::to_lower_copy(verb));
    if(search != command_objs.end()) {
        cmd = search->second;
        return true;
    } else {
        return false;
    }
}
 */
