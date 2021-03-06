// ==========================================================================
// containerbase.h
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
 #ifndef CONTAINER_BASE_H_
 #define CONTAINER_BASE_H_
 

 struct script_entity;
 struct container_base
 {
	 void _unload_inventory_();
	 void recursive_unload(script_entity* se);
	 
     virtual script_entity * GetOwner() = 0;
     virtual bool AddEntityToInventory(script_entity * se);
     
     virtual bool RemoveEntityFromInventoryByID( const std::string& id  );
     
     virtual bool RemoveEntityFromInventory( script_entity * se );
     
     virtual const std::vector< script_entity*  >& GetInventory();
     virtual void ClearInventory();
     protected:
     std::vector< script_entity* > inventory;
    
 };
 #endif