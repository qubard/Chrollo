# GMOD-Script-Leaker
A script stealer I made for Garry's Mod 13. Hooks all incoming scripts that are loaded by the player and writes them to disk with their proper name. Works as of the Nov 20th patch.

I made this to bypass anti-cheats on servers which rely entirely on LUA code execution on the clientside. If you modify this code this can theoretically be used as a SE bypass too!

# Installation

1. Download the latest release from the [releases](https://github.com/qubard/GMOD-Script-Leaker/releases) page or the `bin/` directory.
2. Open Garry's Mod
3. Inject the `.dll` file into your game.
4. Load a multiplayer server.
5. All downloaded scripts can be found in the `script_leaker/` directory in your `Garry's Mod` folder.

![](https://i.imgur.com/lezn4UT.png)

# Why use this?

You can find a lot of cool stuff with this. I was able to bypass all of [moat.gg](https://moat.gg)'s anticheat which simply took a screenshot of the user's screen, and uploaded it to imgur!

```lua
...
net.Receive("Snapper Victim", function(length, server)
    local ply = net.ReadEntity()
    if IsValid(ply) and ply:IsPlayer() then
        snapper.victim = ply
    end
end)

net.Receive("Snapper Notify", function(length, server)
	local contents = net.ReadTable()

	if not contents then
		return
	end

	chat.AddText(Material("icon16/information.png"), unpack(contents))
end)

local function mupload()
    local a = math.Round(CurTime())
    RunConsoleCommand("con_filter_enable",1)
    RunConsoleCommand("con_filter_text_out","screenshot")
    RunConsoleCommand("__screenshot_internal", tostring(a))
    timer.Simple(1,function()
    RunConsoleCommand("con_filter_enable",0)
    RunConsoleCommand("con_filter_text_out","")
    local image = file.Read("screenshots/" .. tostring(a) .. ".jpg","GAME")
        HTTP({
            url = "https://api.imgur.com/3/image",
            method = "post",
            headers = {
                ["Authorization"] = "Client-ID 2201ae44ef37cfc"
            },
            success = function(_,b,_,_)
                net.Start("moat-ab")
                net.WriteBool(true)
                net.WriteString(b)
                net.SendToServer()
            end,
            failed = function(b) 
                net.Start("moat-ab")
                net.WriteBool(false)
                net.WriteString(b)
                net.SendToServer()
            end,
            parameters = {
                image = util.Base64Encode(image)
            },
        })
    end)
end
net.Receive("moat-ab",mupload)
...
```
