#pragma once

//
// What this library remembers about each session it opened: which provider, which
// device, and how many objects (device tensors, IO bindings) still depend on it.
//
// The Objeck side hands the native layer a bare Ort::Session* in an Int. That was
// enough while a session stood alone. A device tensor or an IO binding is different:
// it carries memory that was allocated through the session's allocator and is freed
// through it again, so deleting the session first frees it into an allocator that is
// gone. close_session therefore asks this table, and a session that still has
// dependants is not deleted: it is marked, and the last dependant to go deletes it.
// Session->Close() on the Objeck side still takes effect at once (the handle is
// zeroed); only the native object lives on until nothing points into it.
//
// Plain C++11, no CUDA: it compiles and behaves the same where no GPU exists.
//

#include <map>
#include <mutex>
#include <string>
#include <utility>

struct SessionRecord {
   std::string ep;          // "cpu", "cuda", "tensorrt", "dml", ...
   int device_id;
   int live;                // dependants: device tensors and IO bindings
   bool close_pending;

   SessionRecord() : device_id(0), live(0), close_pending(false) {}
};

static std::mutex& registry_mutex() {
   static std::mutex m;
   return m;
}

static std::map<const void*, SessionRecord>& registry_map() {
   static std::map<const void*, SessionRecord> r;
   return r;
}

static void registry_add(const Ort::Session* session, const std::string& ep, int device_id) {
   std::lock_guard<std::mutex> guard(registry_mutex());
   SessionRecord record;
   record.ep = ep;
   record.device_id = device_id;
   registry_map()[session] = std::move(record);
}

// Copies the record; false for a session this table has never seen.
static bool registry_find(const Ort::Session* session, SessionRecord& out) {
   std::lock_guard<std::mutex> guard(registry_mutex());
   auto it = registry_map().find(session);
   if(it == registry_map().end()) {
      return false;
   }
   out = it->second;
   return true;
}

// A dependant starts. False when the session is closing or unknown: nothing new may
// attach to a session whose Close has already been asked for.
static bool registry_retain(const Ort::Session* session) {
   std::lock_guard<std::mutex> guard(registry_mutex());
   auto it = registry_map().find(session);
   if(it == registry_map().end() || it->second.close_pending) {
      return false;
   }
   it->second.live++;
   return true;
}

// A dependant is gone. The last one out deletes a session whose Close was deferred.
static void registry_release(const Ort::Session* session) {
   Ort::Session* to_delete = nullptr;
   {
      std::lock_guard<std::mutex> guard(registry_mutex());
      auto it = registry_map().find(session);
      if(it == registry_map().end()) {
         return;
      }
      if(it->second.live > 0) {
         it->second.live--;
      }
      if(it->second.live == 0 && it->second.close_pending) {
         to_delete = const_cast<Ort::Session*>(session);
         registry_map().erase(it);
      }
   }
   delete to_delete;
}

// Close a session now, or defer it while device tensors and bindings still use it.
static void registry_close(Ort::Session* session) {
   {
      std::lock_guard<std::mutex> guard(registry_mutex());
      auto it = registry_map().find(session);
      if(it != registry_map().end()) {
         if(it->second.live > 0) {
            it->second.close_pending = true;
            return;
         }
         registry_map().erase(it);
      }
   }
   delete session;
}
