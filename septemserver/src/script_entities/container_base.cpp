#include "stdafx.h"
#include "script_entities/container_base.h"

 #include "script_entities/script_entity.h"
 #include "entity_wrapper.h"
 #include "script_entities/itemobj.h"
 #include "entity_manager.h"

void container_base::recursive_unload(script_entity* se)
{
    if(container_base* cb = dynamic_cast<container_base*>(se)) {
        auto inv = cb->GetInventory();
        for(auto i : inv) {
            recursive_unload(i);
        }
    }
    std::string s = se->GetVirtualScriptPath();
   // entity_manager::Instance().deregister_entity(se);
	entity_manager::Instance().do_delete(se);
	//se->set_destroy(true);
	
}

bool container_base::AddEntityToInventory(script_entity* se)
{
    if(se == NULL)
        return false;

    script_entity* env = se->GetEnvironment();

    if(env != NULL && env == GetOwner())
        return false;
    /*
if(itemobj* item = dynamic_cast<itemobj*>(se)) {
    if(item->get_isStackable()) {
       if( item->get_currentStackCount() > 1 )
       {
           // clone item, decrement count..
            std::string tpath = item->GetBaseScriptPath();
            entity_manager::Instance().clone_item( tpath, GetOwner() );
            itemobj * clone_item = entity_manager::Instance().GetItemByScriptPath(tpath);
            script_entity * se2 = static_cast<script_entity*>(clone_item);
            inventory.insert( se2 );
            item->decrementStackCount();
            auto t = GetOwner();
            if(t == NULL)
                return;
            se2->SetEnvironment(GetOwner());
            return;
       }
    }
}
 */

    if(env != NULL) {
        // remove entity from inventory
        if(container_base* cb = dynamic_cast<container_base*>(env)) {
            cb->RemoveEntityFromInventory(se);
            // se->on_environment_change(EventChangeEvent::REMOVED, env);
        }
    }

    if(itemobj* item = dynamic_cast<itemobj*>(se)) {
        if(item->get_isStackable()) {
            for(script_entity* se_t : inventory) {
                if(itemobj* itemb = dynamic_cast<itemobj*>(se_t)) {
                    if(itemb->GetPhysicalScriptPath() == item->GetPhysicalScriptPath() &&
                       itemb->get_currentStackCount() < itemb->get_defaultStackSize()) {
                        // do merge..
                        unsigned int remain = itemb->get_defaultStackSize() - itemb->get_currentStackCount();
                        unsigned int t_cnt = item->get_currentStackCount();
                        if(t_cnt > remain) {
                            // top off the stack on the ground...
                            itemb->set_currentStackCount(itemb->get_currentStackCount() + remain);
                            item->set_currentStackCount(item->get_currentStackCount() - remain);
                        } else {
                            itemb->set_currentStackCount(itemb->get_currentStackCount() +
                                                         item->get_currentStackCount());
                            item->set_destroy(true); // object is empty, delete it..
                            return true;
                        }
                    }
                }
            }
            assert(item->get_currentStackCount() > 0);
        }
    }
    inventory.push_back(se);
    auto t = GetOwner();
    if(t == NULL)
        return false;
    se->SetEnvironment(GetOwner());
    se->on_environment_change(EnvironmentChangeEvent::ADDED, t);
    return true;
}

bool container_base::RemoveEntityFromInventoryByID(const std::string& id)
{
    // TODO: implement this and be sure to nuke a removed items environment_ pointer..
    std::vector<script_entity*>::iterator it = this->inventory.begin();

    while(it != this->inventory.end()) {
        // Check if key's first character is Fi
        if((*it)->GetInstancePath() == id) {
            (*it)->SetEnvironment(NULL);
            (*it)->on_environment_change(EnvironmentChangeEvent::REMOVED, this->GetOwner());
            it = this->inventory.erase(it);

            return true;
        } else
            it++;
    }
    return false;
}

bool container_base::RemoveEntityFromInventory(script_entity* se)
{
    // TODO: implement this and be sure to nuke a removed items environment_ pointer..
    std::vector<script_entity*>::iterator it = this->inventory.begin();

    while(it != this->inventory.end()) {
        // Check if key's first character is Fi
        if((*it) == se) {
            (*it)->SetEnvironment(NULL);
            (*it)->on_environment_change(EnvironmentChangeEvent::REMOVED, this->GetOwner());
            it = this->inventory.erase(it);

            return true;
        } else
            it++;
    }
    return false;
}

const std::vector<script_entity*>& container_base::GetInventory()
{
    return inventory;
}

void container_base::ClearInventory()
{
    inventory.clear();
}
void container_base::_unload_inventory_()
{
	recursive_unload(GetOwner());
}
