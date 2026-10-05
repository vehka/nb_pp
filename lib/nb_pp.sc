// One Plaits Palette engine. Needs the PlaitsPalette plugin built from ugen/.
PlaitsPalette : MultiOutUGen {
	*ar { |engine=0, pitch=60.0, harm=0.5, timbre=0.5, morph=0.5, macro=0.5,
			gate=1, level=0.8, decay=0.5, lpg_colour=0.5, stereo=0, mul=1.0, add=0.0|
		^this.multiNew('audio', engine, pitch, harm, timbre, morph, macro,
			gate, level, decay, lpg_colour, stereo).madd(mul, add);
	}

	init { |... theInputs|
		inputs = theInputs;
		^this.initOutputs(2, rate);
	}
}

// The sclang half of the nb_pp mod: one synth per note, driven over OSC by
// lib/mod.lua. Each nb voice has a group and a note -> synth table.
NbPP {
	classvar <numVoices = 4;
	classvar <notes, <groups, <engineOf;

	*initClass {
		notes = numVoices.collect { Dictionary.new };
		engineOf = IdentityDictionary.new;

		StartUp.add {
			// after a server boot or cmd-period the old groups are gone
			ServerTree.add({ this.makeGroups }, \all);

			SynthDef(\nbPP, {
				|out, gate=1, perc=0, pitch=60.0, pitch_lag=0.0, engine=0,
				harm=0.5, timbre=0.5, morph=0.5, macro=0.5, level=0.8, decay=0.5,
				lpg_color=0.5, stereo=0, aux_mix=0.0, amp=0.2, gain=1.0, pan=0.0,
				sendA=0, sendB=0, sendABus=0, sendBBus=0|
				// perc: the gate is a 20 ms trigger whatever the note length
				var open = gate * Select.kr(perc, [1, EnvGen.kr(Env([1, 1, 0], [0.02, 0]))]);
				var closed = 1 - open;
				var raw = PlaitsPalette.ar(
					engine: engine,
					pitch: pitch.lag(pitch_lag),
					harm: harm,
					timbre: timbre,
					morph: morph,
					macro: macro,
					gate: open,
					level: level,
					decay: decay,
					lpg_colour: lpg_color,
					stereo: stereo);
				var mono = ((1 - aux_mix) * raw[0]) + (aux_mix * raw[1]);
				var sound = Select.ar(stereo, [
					Pan2.ar(mono, pan),
					Balance2.ar(raw[0], raw[1], pan)
				]);
				sound = LeakDC.ar(sound);
				DetectSilence.ar(sound.sum + Impulse.ar(0), amp: 0.0005, time: 0.1,
					doneAction: Done.freeSelf);
				// engines that never fall silent by themselves
				FreeSelf.kr((Sweep.kr(closed) * closed) > 30);
				sound = (sound * amp * gain).softclip;
				Out.ar(out, sound);
				Out.ar(sendABus, sendA * sound);
				Out.ar(sendBBus, sendB * sound);
			}).add;

			// /nb_pp/note_on voice note key value ...
			OSCFunc({ |msg|
				var voice = msg[1].asInteger, note = msg[2].asInteger;
				var old = notes[voice][note];
				old !? { old.set(\gate, 0) };
				notes[voice][note] = this.spawn(voice, msg[3..]);
			}, "/nb_pp/note_on");

			// /nb_pp/perc voice key value ...
			// a one-shot: nothing keeps track of it, note_off does not reach it
			OSCFunc({ |msg|
				this.spawn(msg[1].asInteger, msg[2..] ++ [\perc, 1]);
			}, "/nb_pp/perc");

			// /nb_pp/mono voice old_note note key value ...
			// moves the sounding synth to the new note, or starts one
			OSCFunc({ |msg|
				var voice = msg[1].asInteger;
				var oldNote = msg[2].asInteger, note = msg[3].asInteger;
				var syn = notes[voice][oldNote];
				// a synth keeps the engine it started with: a changed model
				// needs a new one
				if(syn.notNil and: { engineOf[syn] != this.engineIn(msg[4..]) }) {
					syn.set(\gate, 0);
					notes[voice].removeAt(oldNote);
					syn = nil;
				};
				if(syn.notNil) {
					syn.set(\gate, 1, *msg[4..]);
					if(note != oldNote) {
						notes[voice].removeAt(oldNote);
						notes[voice][note] = syn;
					};
				} {
					notes[voice][note] = this.spawn(voice, msg[4..]);
				};
			}, "/nb_pp/mono");

			// /nb_pp/note_off voice note
			OSCFunc({ |msg|
				var syn = notes[msg[1].asInteger][msg[2].asInteger];
				syn !? { syn.set(\gate, 0) };
			}, "/nb_pp/note_off");

			// /nb_pp/note_set voice note key value
			OSCFunc({ |msg|
				var syn = notes[msg[1].asInteger][msg[2].asInteger];
				syn !? { syn.set(msg[3], msg[4]) };
			}, "/nb_pp/note_set");

			// /nb_pp/voice_set voice key value: every sounding note of a voice
			OSCFunc({ |msg|
				groups !? { groups[msg[1].asInteger].set(msg[2], msg[3]) };
			}, "/nb_pp/voice_set");

			// /nb_pp/stop voice
			OSCFunc({ |msg|
				groups !? { groups[msg[1].asInteger].set(\gate, 0) };
			}, "/nb_pp/stop");

			// /nb_pp/stop_all
			OSCFunc({ |msg|
				groups !? { groups.do { |group| group.set(\gate, 0) } };
			}, "/nb_pp/stop_all");
		}
	}

	// the engine in a key, value argument list
	*engineIn { |args|
		var at = args.indexOf(\engine);
		^at !? { args[at + 1].asInteger };
	}

	*makeGroups {
		groups = numVoices.collect { Group.new(Server.default) };
		notes.do { |dict| dict.clear };
		engineOf.clear;
	}

	*spawn { |voice, args|
		var syn;
		if(groups.isNil) { this.makeGroups };
		syn = Synth.new(\nbPP, args ++ [
			\sendABus, (~sendA ? Server.default.outputBus),
			\sendBBus, (~sendB ? Server.default.outputBus)
		], target: groups[voice]);
		engineOf[syn] = this.engineIn(args);
		syn.onFree({
			// forget it, unless the note has been taken over by a newer synth
			var note = notes[voice].findKeyForValue(syn);
			note !? { notes[voice].removeAt(note) };
			engineOf.removeAt(syn);
		});
		^syn;
	}
}
