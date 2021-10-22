#include <cstdint>

#include <imgui.h>

#include "../sdk/REContext.hpp"
#include "../sdk/RETypeDB.hpp"

#include "ScriptRunner.hpp"

namespace api::re {
void msg(const char* text) {
    MessageBox(g_framework->get_window(), text, "ScriptRunner Message", MB_ICONINFORMATION | MB_OK);
}
}

namespace api::sdk {
void* get_thread_context() {
    return (void*)::sdk::get_thread_context();
}

void* find_type_definition(const char* name) {
    return (void*)::sdk::RETypeDB::get()->find_type(name);
}

void* get_native_singleton(const char* name) {
    return (void*)::sdk::get_native_singleton<void>(name);
}

void* get_managed_singleton(const char* name) {
    return (void*)g_framework->get_globals()->get(name);
}

void* call_native_func(void* obj, void* def, const char* name, sol::variadic_args va) {
    static std::vector<void*> args{};
    auto l = va.lua_state();

    args.clear();

    for (auto&& arg : va) {
        auto i = arg.stack_index();

        // sol2 doesn't seem to differentiate between Lua integers and numbers. So
        // we must do it ourselves.
        if (lua_isinteger(l, i)) {
            auto n = (intptr_t)lua_tointeger(l, i);
            args.push_back((void*)n);
        } else if (lua_isnumber(l, i)) {
            auto f = lua_tonumber(l, i);
            auto n = *(intptr_t*)&f;
            args.push_back((void*)n);
        } else {
            args.push_back(arg.as<void*>());
        }
    }

    return ::sdk::invoke_object_func(obj, (::sdk::RETypeDefinition*)def, name, args);
}

auto call_object_func(void* obj, const char* name, sol::variadic_args va) {
    auto def = utility::re_managed_object::get_type_definition((::REManagedObject*)obj);

    return call_native_func((void*)obj, def, name, va);
}
}

ScriptState::ScriptState() {
    m_lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string, sol::lib::math, sol::lib::table, sol::lib::bit32, sol::lib::utf8);

    auto re = m_lua.create_table();
    re["msg"] = api::re::msg;
    re["on_pre_application_entry"] = [this](const char* name, sol::function fn) { m_pre_application_entry_fns.emplace(name, fn); };
    re["on_application_entry"] = [this](const char* name, sol::function fn) { m_application_entry_fns.emplace(name, fn); };
    m_lua["re"] = re;

    auto sdk = m_lua.create_table();
    sdk["get_thread_context"] = api::sdk::get_thread_context;
    sdk["get_native_singleton"] = api::sdk::get_native_singleton;
    sdk["get_managed_singleton"] = api::sdk::get_managed_singleton;
    sdk["find_type_definition"] = api::sdk::find_type_definition;
    sdk["call_native_func"] = api::sdk::call_native_func;
    sdk["call_object_func"] = api::sdk::call_object_func;
    m_lua["sdk"] = sdk;
}

void ScriptState::run_script(const std::string& p) {
    /*m_lua.safe_script_file(p, [](lua_State*, sol::protected_function_result pfr) {
        if (!pfr.valid()) {
            sol::error err = pfr;
            api::re::msg(err.what());
        }
        return pfr;
    });*/
    try {
        m_lua.safe_script_file(p);
    } catch (const std::exception& e) {
        api::re::msg(e.what());
    }
}

void ScriptState::on_pre_application_entry(const char* name) {
    auto range = m_pre_application_entry_fns.equal_range(name);

    for (auto it = range.first; it != range.second; ++it) {
        it->second();
    }
}

void ScriptState::on_application_entry(const char* name) {
    auto range = m_application_entry_fns.equal_range(name);

    for (auto it = range.first; it != range.second; ++it) {
        it->second();
    }
}

void ScriptRunner::on_draw_ui() {
    ImGui::SetNextTreeNodeOpen(false, ImGuiCond_::ImGuiCond_Once);

    if (!ImGui::CollapsingHeader(get_name().data())) {
        return;
    }

    if (ImGui::Button("Run script")) {
        OPENFILENAME ofn{};
        char file[260]{};

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_framework->get_window();
        ofn.lpstrFile = file;
        ofn.nMaxFile = sizeof(file);
        ofn.lpstrFilter = "Lua script files (*.lua)\0*.lua\0";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

        if (GetOpenFileName(&ofn) != FALSE) {
            m_state.run_script(file);
        }
    }

    if (ImGui::Button("Reset scripts")) {
        m_state = {};
    }
}

void ScriptRunner::on_pre_application_entry(void* entry, const char* name, size_t hash) {
    m_state.on_pre_application_entry(name);
}

void ScriptRunner::on_application_entry(void* entry, const char* name, size_t hash) {
    m_state.on_application_entry(name);
}
