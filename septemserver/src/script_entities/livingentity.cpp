#include "script_entities/livingentity.h"
#include "entity_manager.h"

roomobj* living_entity::GetRoom()
{
    assert(GetEnvironment() != NULL);
    return dynamic_cast<roomobj*>(GetEnvironment());
}

void lua_stacktrace(lua_State* L)
{

    lua_Debug entry;
    int depth = 1;

    while(lua_getstack(L, depth, &entry)) {
        lua_Debug info;
        // Level 0 means current function (this C function, which is useless for our purposes)
        // Level 1 means nect call frame up the stack. This is probably the environment we're looking for?
        //?int level = 1;
        int pre_stack_size = lua_gettop(L);
        if(lua_getstack(L, depth, &info) != 1) {
            // failure: call it quits
            std::cout << "error: unable to traverse the stack" << std::endl;
            lua_settop(L, pre_stack_size);
            return;
        }
        // the "f" identifier is the most important here
        // it pushes the function running at `level` onto the stack:
        // we can get the environment from this
        // the rest is for printing / debugging purposes
        if(lua_getinfo(L, "fnluS", &info) == 0) {
            // failure?
            std::cout << "error: unable to get stack information" << std::endl;
            lua_settop(L, pre_stack_size);
            return;
        }

        // Okay, so all the calls worked.
        // Print out some information about this "level"
        std::cout << "[" << depth << "] " << info.short_src << ":" << info.currentline << " -- "
                  << (info.name ? info.name : "<unknown>") << "[" << info.what << "]" << std::endl;

        // Grab the function off the top of the stack
        // remember: -1 means top, -2 means 1 below the top, and so on...
        // 1 means the very bottom of the stack,
        // 2 means 1 more up, and so on to the top value...
        sol::function f(L, -1);
        // The environment can now be ripped out of the function
        sol::environment env(sol::env_key, f);
        if(!env.valid()) {
            std::cout << "error: no environment to get" << std::endl;
            lua_settop(L, pre_stack_size);
            return;
        }
        sol::state_view lua(L);
        sol::optional<std::string> s = env["_INTERNAL_SCRIPT_PATH_"];
        if(s)
            std::cout << s.value() << std::endl;
        // sol::environment freshenv = lua["cmds_env_"]["move_env_"];
        // bool is_same_env = freshenv == env;
        // std::cout << "env == freshenv : " << is_same_env << std::endl;
        depth++;
    }
}

bool living_entity::DoCommand(sol::this_state ts, const std::string& cmd)
{
    /*
       lua_State* L = ts;
       lua_stacktrace(L);
       sol::object selfobj = sol::object(L, 1);
       script_entity& self = selfobj.as<script_entity>();
   */
    // lua_stacktrace(ts);

    // return NULL;
    return entity_manager::Instance().do_command(this, cmd);
}

void living_entity::SendToEnvironment(const std::string& msg)
{
    roomobj* r = this->GetRoom();
    if(r == NULL)
        return;

    for(auto o : r->GetInventory()) {
        if(o != this) {
            if(auto p = dynamic_cast<playerobj*>(o)) {
                p->SendToEntity(msg);
            }
        }
    }
}

bool living_entity::AddInventorySlot(InventorySlot slot, unsigned int maxItems, ItemSize maxItemSize)
{

    // if( m_inventory_slots.find(slotName) != m_inventory_slots.end() )
    //{
    ///    return false;
    //}
    m_inventory_slots[slot] = std::shared_ptr<inventory_slot>(new inventory_slot(slot, maxItems, maxItemSize));
    return true;
}

bool living_entity::AddEntityToInventory(script_entity* se)
{
    if( auto item = dynamic_cast<itemobj*>(se) )
    {
        // update inventory slots..
        auto inv_slots = GetInventorySlots();
        for (auto s : inv_slots )
        {
            //if( se->
            if(  s->get_inventorySlot() & item->get_inventorySlot() )
            {
                if( s->get_itemCount() >= s->get_maxItems() )
                {
                    return false;
                }
            }
        }
    }
    bool b = container_base::AddEntityToInventory(se);
    this->do_save();
    return b;
}

bool living_entity::RemoveEntityFromInventory(script_entity* se)
{
    bool b = container_base::RemoveEntityFromInventory(se);
    this->do_save();
    return b;
}

std::vector<itemobj*> living_entity::GetItems()
{
    std::vector<itemobj*> t_items;
    for(auto i : this->GetInventory()) {
        if( auto i_maybe = dynamic_cast<itemobj*>(i) )
        {
            t_items.push_back(i_maybe);
        }
    }

    return t_items;
}

std::vector<inventory_slot*> living_entity::GetInventorySlots()
{
    std::vector<inventory_slot*> slots;
    slots.reserve(m_inventory_slots.size());
    /*
    if( auto item = dynamic_cast<itemobj*>(se) )
    {
        // update inventory slots..
        auto inv_slots = GetInventorySlots();
        for (auto s : inv_slots )
        {
            //if( se->
            if(  s->get_inventorySlot() & item->get_inventorySlot() )
            {
                if( s->get_itemCount() >= s->get_maxItems() )
                {
                    return false;
                }
                else
                {
                    s->incrementItemCount();
                }
                
            }
        }
    }
     */
    auto items = GetItems();
    
     
    for(auto kvp : m_inventory_slots) 
    {
        kvp.second.get()->set_itemCount(0);
        for( auto i : items )
        {
            if( i->get_inventorySlot() & kvp.first )
            {
                kvp.second.get()->incrementItemCount();
            }
        }
        slots.push_back(kvp.second.get());
    }
    return slots;
}

std::vector<itemobj*> living_entity::GetItemsInInventorySlot(InventorySlot s)
{
    auto items = GetItems();
    std::vector<itemobj*> found;
    for( auto i : items )
    {
        if( i->get_inventorySlot() & s )
        {
            found.push_back(i);
        }
    }
    return found;
}
