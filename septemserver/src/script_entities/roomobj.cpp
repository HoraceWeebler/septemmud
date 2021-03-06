#include "stdafx.h"
#include "script_entities/roomobj.h"
#include "script_entities/livingentity.h"
#include "script_entities/playerobj.h"
#include "entity_manager.h"

#include "script_entities/doorobj.h"
#include "script_entities/lookobj.h"
#include "script_entities/exitobj.h"
#include "script_entities/npcobj.h"

roomobj::roomobj(sol::this_state ts, sol::this_environment te )
    : script_entity(ts, te, EntityType::ROOM, "")
    {
    room_type = RoomType::INDOOR;
	biome_type = BiomeType::ZT_NONE;
	
    entity_manager::Instance().register_room(this);
}


roomobj::roomobj(sol::this_state ts, sol::this_environment te, int rt)
    : script_entity(ts, te, EntityType::ROOM, "")
    {
    room_type = static_cast<RoomType>(rt);
	biome_type = BiomeType::ZT_NONE;
	
    entity_manager::Instance().register_room(this);
}

roomobj::roomobj(sol::this_state ts, sol::this_environment te, int rt, int zt)
    : script_entity(ts, te, EntityType::ROOM, "")
    {
    room_type = static_cast<RoomType>(rt);
	biome_type = static_cast<BiomeType>(zt);
	
    entity_manager::Instance().register_room(this);
}


roomobj::~roomobj()
{
	//_unload_inventory_();
    //this->inventory.clear();
    // entity_manager::Instance().deregister_room(this);
}

std::vector<script_entity*> roomobj::GetPlayers(const std::string& name)
{
    std::vector<script_entity*> players;
    for(auto obj : GetInventory()) {
        if(obj->GetType() == EntityType::PLAYER) {
            // playerobj * p = dynamic_cast<playerobj*>(obj);
            if(boost::to_lower_copy(obj->GetName()) != boost::to_lower_copy(name)) {
                players.push_back(obj);
            }
        }
    }
    return players;
}

std::vector<itemobj*> roomobj::GetItems()
{
    std::vector<itemobj*> items;
    for(auto obj : GetInventory()) {
        if(obj->GetType() == EntityType::ITEM) {
            // playerobj * p = dynamic_cast<playerobj*>(obj);
            // if(boost::to_lower_copy(obj->GetName()) != boost::to_lower_copy(name)) {
            items.push_back(static_cast<itemobj*>(obj));
            // }
        }
    }
    return items;
}

std::vector<npcobj*> roomobj::GetNPCs()
{
    std::vector<npcobj*> npcs;
    for(auto obj : GetInventory()) {
        if(obj->GetType() == EntityType::NPC) {
            // playerobj * p = dynamic_cast<playerobj*>(obj);
            // if(boost::to_lower_copy(obj->GetName()) != boost::to_lower_copy(name)) {
            npcs.push_back(static_cast<npcobj*>(obj));
            // }
        }
    }
    return npcs;
}

void roomobj::debug(const std::string& msg)
{
    for(auto obj : GetInventory()) {
        if(obj->GetType() == EntityType::PLAYER) {
            playerobj* p = dynamic_cast<playerobj*>(obj);
            if(p->isCreator())
                p->SendToEntity("DEBUG: " + msg);
        }
    }
    script_entity::debug(msg);
}

void roomobj::SendToRoom(const std::string& msg)
{
    for(auto obj : GetInventory()) {
        if(obj->GetType() == EntityType::PLAYER || obj->GetType() == EntityType::NPC) {
            living_entity* p = dynamic_cast<living_entity*>(obj);
            p->SendToEntity(msg);
            // if( p->isCreator() )
            //    p->SendToEntity("DEBUG: " + msg);
        }
    }
}

doorobj* roomobj::AddDoor(const std::string& door_name,
                          const std::string& door_path,
                          const unsigned int door_id,
                          bool open,
                          bool locked)
{
    // TODO: add in validation code
    std::shared_ptr<doorobj> new_door =
        std::shared_ptr<doorobj>(new doorobj(door_name, door_path, door_id, open, locked));
    std::string doorp = door_path;
    roomobj* maybe_otherside = entity_manager::Instance().GetRoomByScriptPath(doorp);
    if(maybe_otherside != NULL) {
        // new_door->set_otherSide(maybe_otherside);
        // new_door->set_isMaster(false);

        if(doorobj* dobj = maybe_otherside->GetDoorByID(door_id)) {
            // dobj->set_otherSide(this);

        } else {
            // add it in.
            maybe_otherside->AddDoor(door_name, this->GetInstancePath(), door_id, open, locked);
        }

        // maybe_otherside->set_otherSide(new_door); // first door intantiated wins!
        // maybe_otherside->set_isMaster(true);
    } else {
        // new_door->set_otherSide(NULL);
        // new_door->set_isMaster(true);
    }

    new_door->SetRoomOwner(this);
    doors.push_back(new_door);

    return new_door.get();
}

exitobj* roomobj::AddExit( sol::this_state ts,
						  sol::as_table_t<std::vector<std::string> > exit,
                          sol::object exit_path,
                          bool obvious)
{
	lua_State* L = ts;
   // lua_stacktrace_ex(L);
    sol::type t = exit_path.get_type();
	std::string ext = "";
    unsigned int ret = -1;
    switch(t) {
    case sol::type::string: {
		ext = exit_path.as<std::string>();
    } break;
    case sol::type::userdata: {
		script_entity * se = exit_path.as<script_entity*>();
		auto r_t = dynamic_cast<roomobj*>(se);
		if(r_t)
		{
			ext = r_t->GetInstancePath();
		}
    } break;
    default:
        // std::cout << "";
        break;
    }
	if(ext.empty())
		return NULL;
	
    // TODO: add in validation code
    obvious_exits.push_back(std::shared_ptr<exitobj>(new exitobj(exit, ext, obvious)));
    return obvious_exits[obvious_exits.size() - 1].get();
}

lookobj* roomobj::AddLook(sol::as_table_t<std::vector<std::string> > look,
                          const std::string& description)
{
    looks.push_back(std::shared_ptr<lookobj>(new lookobj(look, description)));
    return looks[looks.size() - 1].get();
}

script_entity * roomobj::GetOwner()
{
    return this;
}

std::vector<std::shared_ptr<exitobj>>& roomobj::GetExits()
    {
        return obvious_exits;
    }
    
std::vector<std::shared_ptr<doorobj>>& roomobj::GetDoors()
{
    return doors;
}

doorobj * roomobj::GetDoorByID(const unsigned int did)
{
    for( auto d : this->doors )
    {
        if( d->get_doorID() == did )
            return d.get();
    }
    return NULL;
}

std::vector<std::shared_ptr<lookobj>>& roomobj::GetLooks()
{
    return looks;
}


void roomobj::SetTitle(const std::string& title)
{
    this->title = title;
}
const std::string& roomobj::GetTitle()
{
    return title;
}

void roomobj::SetDescription(const std::string& description)
{
    this->description = description;
}
const std::string& roomobj::GetDescription()
{
    return description;
}

void roomobj::SetShortDescription(const std::string& short_description)
{
    this->short_description = short_description;
}
const std::string& roomobj::GetShortDescription()
{
    return short_description;
}

bool roomobj::AddEntityToInventory(script_entity * se) 
{
     return container_base::AddEntityToInventory(se);
    // se->SetEnvironment( static_cast<script_entity*>(this) );
     //se->SetEnvironment(static_cast<script_entity*>(this)); // this must be called AFTER the above (to remove the se from its previous owner)
}
 
bool roomobj::RemoveEntityFromInventoryByID( const std::string& id  ) 
{
    return container_base::RemoveEntityFromInventoryByID(id);
     // TODO: implement this and be sure to nuke a removed items environment_ pointer..
     
}

bool roomobj::RemoveEntityFromInventory( script_entity * se ) 
{
    //se->SetEnvironment(NULL);
    return container_base::RemoveEntityFromInventory(se);
}

bool roomobj::GetIsOutdoor()
{
    if( room_type == RoomType::OUTDOOR )
    {
        return true;
    }
    else return false;
}

void roomobj::SetBiomeType(BiomeType zt)
{
	biome_type = zt;
}
	
void roomobj::SetRoomType(RoomType rt)
{
	room_type = rt;
}
	
RoomType roomobj::GetRoomType()
{
	return room_type;
}
	
BiomeType roomobj::GetBiomeType()
{
	return biome_type;
}

