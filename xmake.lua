add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

-- 要对着哪个 LeviLamina 版本编译：
--   26.20.*  -> BDS 1.26.20.x
--   1.4.*    -> BDS 1.21.93.x
-- 两个服务端版本不一样的话，就各编译一份。
option("ll_version")
    set_default("26.20.*")
    set_showmenu(true)
    set_description("LeviLamina version, e.g. 26.20.* or 1.4.*")
option_end()

-- 首次 configure 时 xmake 还没应用命令行配置，get_config 会是 nil；
-- 这里给默认值，避免 "attempt to concatenate a nil value"。
local ll_version  = get_config("ll_version") or "26.20.*"
local target_type = get_config("target_type") or "server"

add_requires("levilamina " .. ll_version, {configs = {target_type = target_type}})

add_requires("levibuildscript")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("AntiBang")
    add_rules("@levibuildscript/linkrule")
    -- 显式给版本号，避免 modpacker 在非 git 目录里执行 `git describe` 失败。
    add_rules("@levibuildscript/modpacker", {modVersion = "1.0.0"})
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE")
        set_exceptions("none") -- 避免和 /EHa 冲突
        add_cxflags("/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
        set_toolchains("clang-cl")
    end
    add_packages("levilamina")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
    if is_config("target_type", "server") then
        add_defines("ANTIBANG_SERVER")
    end
