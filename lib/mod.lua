-- nb_pp
-- Plaits Palette engines as nb voices.
--
-- each note is a synth in SuperCollider (lib/nb_pp.sc) playing one engine
-- through the PlaitsPalette UGen (ugen/). this file is the nb players,
-- their params, and a mod menu choosing which of the engines the model
-- param steps through.

local mod = require 'core/mods'
local music = require 'musicutil'

local mod_name = mod.this_name or "nb_pp"
local engines = require(mod_name .. '/lib/engines')

local NUM_VOICES = 4
local SC = { "localhost", 57120 }
local DATA_DIR = _path.data .. "nb_pp/"
local PREFS_FILE = DATA_DIR .. "prefs.data"
local STYLES = { "poly", "mono", "perc" }
local POLY, MONO, PERC = 1, 2, 3
-- the params whose names follow the model, in the order of engine.controls
local CONTROL_PARAMS = { "harmonics", "timbre", "morph", "macro" }

-- the UGen is downloaded and installed by norns-deps (lib/deps)
local deps = dofile(_path.code .. mod_name .. "/lib/deps.lua")
local d = deps.new { name = "nb_pp", dir = DATA_DIR .. "deps" }
d:add {
  id = "nb_pp", label = "PlaitsPalette UGen", size = "0.4 MB",
  why = "the synthesis engines",
  check_ugens = "PlaitsPalette",
  -- built on the device and attached to a release; a new build goes in a
  -- new release (ugen-2), with its checksum here
  ugens = "https://github.com/vehka/nb_pp/releases/download/ugen-1/"
    .. "PlaitsPalette-{arch}.tar.gz",
  sha256 = {
    armv7l = "5ac1de2b9c04a21151b581a437c38c5a5a0974c134ca396c8521fdae831a9bef",
  },
  manual = "build it: ugen/build.sh install (see README)",
}

-- disabled[engine id] = true: the model param skips the engine
-- no_prompt: the install offer was turned down, don't open it at every script
local prefs = { disabled = {} }

local function read_prefs()
  if util.file_exists(PREFS_FILE) then
    local saved = tab.load(PREFS_FILE)
    if type(saved) == "table" and type(saved.disabled) == "table" then
      prefs.disabled = saved.disabled
      prefs.no_prompt = saved.no_prompt
    end
  end
end

local function write_prefs()
  util.make_dir(DATA_DIR)
  tab.save(prefs, PREFS_FILE)
end

local function enabled(index)
  return not prefs.disabled[engines[index].id]
end

local function ugen_installed()
  return d:ok("nb_pp")
end

-- open the installer screen; on_close runs when it has closed
local function install_ugen(on_close)
  d:ensure({ "nb_pp" }, { on_done = function(ok)
    prefs.no_prompt = (not ok) or nil
    write_prefs()
    if on_close then on_close() end
  end })
end

local function send(path, args)
  osc.send(SC, path, args)
end

------------------------------------------------------------------------
-- params

local function p(i, name)
  return "nb_pp_" .. i .. "_" .. name
end

-- the position of the engine `steps` enabled engines away from `from`
local function step_model(from, steps)
  local dir = steps > 0 and 1 or -1
  local index = from
  for _ = 1, math.abs(steps) do
    local nxt = index + dir
    while engines[nxt] and not enabled(nxt) do
      nxt = nxt + dir
    end
    if not engines[nxt] then break end
    index = nxt
  end
  return index
end

local function control_label(text)
  text = text:lower()
  if #text > 16 then text = text:sub(1, 15) .. "~" end
  return text
end

local function name_controls(i, engine)
  for n, name in ipairs(CONTROL_PARAMS) do
    params:lookup_param(p(i, name)).name = control_label(engine.controls[n])
  end
end

local function add_voice_params(i)
  params:add_group(p(i, "group"), "palette " .. i, 16)
  params:hide(p(i, "group"))

  params:add_option(p(i, "style"), "style", STYLES, POLY)
  -- start on an engine that is enabled; a pset may still name one that is not
  local first = enabled(1) and 1 or step_model(1, 1)
  params:add_number(p(i, "model"), "model", 1, #engines, first, function(param)
    return engines[param:get()].name
  end)
  -- encoder turns only land on engines enabled in the mod menu. set() still
  -- reaches every engine, so psets and MIDI mapping keep working.
  params:lookup_param(p(i, "model")).delta = function(self, d)
    if d ~= 0 then self:set(step_model(self:get(), d)) end
  end

  local unipolar = controlspec.new(0, 1, "lin", 0, 0.5)
  params:add_control(p(i, "harmonics"), "harmonics", unipolar)
  params:add_control(p(i, "timbre"), "timbre", unipolar)
  params:add_control(p(i, "morph"), "morph", unipolar)
  params:add_control(p(i, "macro"), "macro", unipolar)
  params:add_control(p(i, "decay"), "decay", unipolar)
  params:add_control(p(i, "lpg_color"), "lpg color", unipolar)
  params:add_control(p(i, "amp"), "amp", controlspec.new(0, 1, "lin", 0, 0.2))
  params:add_option(p(i, "stereo"), "stereo", { "off", "on" }, 1)
  params:add_control(p(i, "aux_mix"), "aux mix", controlspec.new(0, 1, "lin", 0, 0))
  params:add_control(p(i, "gain"), "gain", controlspec.new(0, 3, "lin", 0, 1))
  params:add_control(p(i, "pan"), "pan", controlspec.new(-1, 1, "lin", 0, 0))
  params:add_control(p(i, "slew"), "slew", controlspec.new(0, 1, "lin", 0, 0, "s"))
  params:add_control(p(i, "send_a"), "send a", controlspec.new(0, 1, "lin", 0, 0))
  params:add_control(p(i, "send_b"), "send b", controlspec.new(0, 1, "lin", 0, 0))

  params:set_action(p(i, "style"), function(style)
    if style == MONO then
      params:show(p(i, "slew"))
    else
      params:hide(p(i, "slew"))
    end
    _menu.rebuild_params()
    send("/nb_pp/stop", { i - 1 })
  end)
  params:set_action(p(i, "model"), function(index)
    name_controls(i, engines[index])
  end)
  -- sounding notes follow these; the engine and amp are fixed at note on
  local live = {
    harmonics = "harm", timbre = "timbre", morph = "morph", macro = "macro",
    decay = "decay", lpg_color = "lpg_color", aux_mix = "aux_mix",
    gain = "gain", pan = "pan", send_a = "sendA", send_b = "sendB",
  }
  for name, key in pairs(live) do
    params:set_action(p(i, name), function(value)
      send("/nb_pp/voice_set", { i - 1, key, value })
    end)
  end
  params:set_action(p(i, "stereo"), function(value)
    send("/nb_pp/voice_set", { i - 1, "stereo", value - 1 })
  end)
end

------------------------------------------------------------------------
-- nb players

-- fractional MIDI note, through musicutil so that tuning mods apply
local function pitch_of(note)
  local hz = music.note_num_to_freq(note % 128)
  return 69 + 12 * math.log(hz / 440) / math.log(2)
end

local function add_player(i)
  local player = {
    timbre_modulation = 0,
  }

  -- the synth arguments of a new note, as a flat key, value list
  local function note_args(note, vel, properties)
    properties = properties or {}
    return {
      "pitch", pitch_of(note),
      "engine", engines[params:get(p(i, "model"))].ugen,
      "harm", params:get(p(i, "harmonics")) + (properties.harmonics or 0),
      "timbre", params:get(p(i, "timbre")) + player.timbre_modulation / 2
        + (properties.timbre or 0),
      "morph", params:get(p(i, "morph")) + (properties.morph or 0),
      "macro", params:get(p(i, "macro")),
      "level", vel,
      "decay", params:get(p(i, "decay")),
      "lpg_color", params:get(p(i, "lpg_color")),
      "stereo", params:get(p(i, "stereo")) - 1,
      "aux_mix", params:get(p(i, "aux_mix")),
      "amp", params:get(p(i, "amp")),
      "gain", params:get(p(i, "gain")),
      "pan", params:get(p(i, "pan")),
      "sendA", params:get(p(i, "send_a")),
      "sendB", params:get(p(i, "send_b")),
    }
  end

  local function prepend(args, ...)
    local head = { ... }
    for _, v in ipairs(args) do head[#head + 1] = v end
    return head
  end

  -- per-note changes reach a note that still has a synth of its own
  local function addressable(note)
    local style = params:get(p(i, "style"))
    return style == POLY or (style == MONO and note == player.current_note)
  end

  function player:add_params()
    add_voice_params(i)
  end

  function player:describe()
    return {
      name = "palette " .. i,
      supports_bend = true,
      supports_slew = params:get(p(i, "style")) == MONO,
      modulate_description = "timbre",
      note_mod_targets = { "amp", "timbre", "morph", "harmonics" },
      params = {
        p(i, "model"), p(i, "harmonics"), p(i, "timbre"), p(i, "morph"),
        p(i, "macro"), p(i, "decay"), p(i, "lpg_color"), p(i, "amp"),
      },
    }
  end

  function player:active()
    if self.name ~= nil then
      params:show(p(i, "group"))
      _menu.rebuild_params()
    end
  end

  function player:inactive()
    if self.name ~= nil then
      params:hide(p(i, "group"))
      _menu.rebuild_params()
    end
  end

  function player:stop_all()
    send("/nb_pp/stop", { i - 1 })
  end

  function player:modulate(val)
    self.timbre_modulation = val
  end

  function player:set_slew(s)
    params:set(p(i, "slew"), s)
  end

  function player:pitch_bend(note, amount)
    if addressable(note) then
      send("/nb_pp/note_set", { i - 1, note, "pitch", pitch_of(note) + amount })
    end
  end

  function player:modulate_note(note, key, value)
    if not addressable(note) then return end
    if key == "amp" then
      value = util.clamp(value ^ (3 / 2) * params:get(p(i, "amp")), 0.003, 1)
    elseif key == "harmonics" or key == "timbre" or key == "morph" then
      value = value + params:get(p(i, key))
      if key == "harmonics" then key = "harm" end
    else
      return
    end
    send("/nb_pp/note_set", { i - 1, note, key, value })
  end

  function player:note_on(note, vel, properties)
    local style = params:get(p(i, "style"))
    local args = note_args(note, vel, properties)
    if style == PERC then
      send("/nb_pp/perc", prepend(args, i - 1))
    elseif style == POLY then
      send("/nb_pp/note_on", prepend(args, i - 1, note))
    else
      args[#args + 1] = "pitch_lag"
      args[#args + 1] = params:get(p(i, "slew"))
      send("/nb_pp/mono", prepend(args, i - 1, self.current_note or note, note))
      self.current_note = note
    end
  end

  function player:note_off(note)
    if params:get(p(i, "style")) ~= PERC then
      send("/nb_pp/note_off", { i - 1, note })
    end
  end

  if note_players == nil then
    note_players = {}
  end
  note_players["palette " .. i] = player
end

mod.hook.register("script_pre_init", "nb_pp pre init", function()
  read_prefs()
  for i = 1, NUM_VOICES do
    add_player(i)
  end
end)

-- offer to install the UGen, or to restart after an install. once turned
-- down, the offer stays in the mod menu only.
mod.hook.register("script_post_init", "nb_pp post init", function()
  if d:restart_pending() then
    install_ugen()
  elseif not ugen_installed() then
    print("nb_pp: PlaitsPalette UGen not installed, voices will be silent."
      .. " install it from SYSTEM > MODS > NB_PP")
    if not prefs.no_prompt then install_ugen() end
  end
end)

mod.hook.register("script_post_cleanup", "nb_pp post cleanup", function()
  send("/nb_pp/stop_all", {})
end)

------------------------------------------------------------------------
-- mod menu: which engines the model param steps through
--
-- E2 scroll, E3 jump to the next family, K3 toggle an engine (or, on a
-- family line, the whole family), K2 back. without the UGen, K3 installs it.

local ROWS = 6

local m = {
  rows = {},  -- { family = name, members = { index... } } or { engine = index }
  pos = 1,
  ugen = true,
}

local function build_rows()
  local rows, by_family = {}, {}
  local families = {}
  for index, engine in ipairs(engines) do
    if not by_family[engine.family] then
      by_family[engine.family] = {}
      families[#families + 1] = engine.family
    end
    table.insert(by_family[engine.family], index)
  end
  for _, family in ipairs(families) do
    rows[#rows + 1] = { family = family, members = by_family[family] }
    for _, index in ipairs(by_family[family]) do
      rows[#rows + 1] = { engine = index }
    end
  end
  return rows
end

local function count_enabled(members)
  local n = 0
  for _, index in ipairs(members) do
    if enabled(index) then n = n + 1 end
  end
  return n
end

local function set_enabled(index, on)
  prefs.disabled[engines[index].id] = (not on) or nil
end

function m.init()
  read_prefs()
  if #m.rows == 0 then m.rows = build_rows() end
  m.ugen = ugen_installed()
end

function m.deinit()
  write_prefs()
end

function m.key(n, z)
  if z ~= 1 then return end
  if n == 2 then
    mod.menu.exit()
  elseif n == 3 and not m.ugen then
    install_ugen(function() m.ugen = ugen_installed() end)
  elseif n == 3 then
    local row = m.rows[m.pos]
    if row.engine then
      set_enabled(row.engine, not enabled(row.engine))
    else
      -- a family with anything off turns all on; a full one turns all off
      local on = count_enabled(row.members) < #row.members
      for _, index in ipairs(row.members) do
        set_enabled(index, on)
      end
    end
    mod.menu.redraw()
  end
end

function m.enc(n, d)
  if n == 2 then
    m.pos = util.clamp(m.pos + d, 1, #m.rows)
  elseif n == 3 then
    local dir = d > 0 and 1 or -1
    local pos = m.pos + dir
    while m.rows[pos] and not m.rows[pos].family do
      pos = pos + dir
    end
    -- past the first or last family: go to the end of the list
    m.pos = m.rows[pos] and pos or (dir > 0 and #m.rows or 1)
  end
  mod.menu.redraw()
end

function m.redraw()
  screen.clear()
  screen.level(4)
  screen.move(0, 7)
  screen.text(m.ugen and "NB_PP" or "NB_PP: K3 INSTALL UGEN")
  local all = {}
  for index = 1, #engines do all[index] = index end
  screen.move(127, 7)
  screen.text_right(count_enabled(all) .. "/" .. #engines)

  -- keep the selected row third from the top while there is room
  local first = util.clamp(m.pos - 2, 1, math.max(1, #m.rows - ROWS + 1))
  for line = 1, ROWS do
    local pos = first + line - 1
    local row = m.rows[pos]
    if not row then break end
    local y = 8 + line * 9
    local selected = pos == m.pos
    if row.family then
      screen.level(selected and 15 or 6)
      screen.move(0, y)
      screen.text(row.family:upper())
      screen.move(127, y)
      screen.text_right(count_enabled(row.members) .. "/" .. #row.members)
    else
      local on = enabled(row.engine)
      screen.level(selected and 15 or (on and 5 or 2))
      if on then
        screen.rect(5, y - 5, 4, 4)
        screen.fill()
      else
        -- a stroke is drawn around the path, half a pixel out
        screen.rect(6, y - 4, 3, 3)
        screen.stroke()
      end
      screen.move(13, y)
      screen.text(engines[row.engine].name)
    end
  end
  screen.update()
end

mod.menu.register(mod_name, m)
