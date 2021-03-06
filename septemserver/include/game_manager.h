// ==========================================================================
// game_manager.h
//
// Copyright (C) 2018 Kenneth Thompson, All Rights Reserved.
// This file is covered by the MIT Licence:
//
// Permission is hereby granted, free of charge, to any person obtaining a 
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation 
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the 
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
// DEALINGS IN THE SOFTWARE.
// ==========================================================================
#ifndef GAME_MANAGER_H_
#define GAME_MANAGER_H_

#include "entity_manager.h"
#include "script_entities/script_entity.h"
#include "script_entities/daemonobj.h"

namespace ba = boost::asio;

enum class gameState { STOPPED, INITIALIZING, RUNNING, STOPPING, ERROR };

struct game_manager
{
    game_manager()
    ;
    
    bool start()
    ;
    
    bool stop()
    ;
    
    void do_heartbeats()
    ;
    
    bool move_entity_into_room( script_entity* en, std::string room_path );
    
   // roomobj* get_void_room();

    
    bool process_player_cmd(playerobj* p, std::string& cmd);
    
   
    
private:
    gameState m_state;
    
    bool SetState( gameState new_state );
    
    void init();
    
    void do_stop();
    
    void on_error(std::string err);
    

};

#endif