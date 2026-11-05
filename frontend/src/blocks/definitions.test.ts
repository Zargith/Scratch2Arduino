import * as Blockly from "scratch-blocks";
import { describe, expect, it } from "vitest";

import { defineBlocks } from "@/blocks/definitions";

describe("block definitions", () => {
	it("shows the frequency over the note menu and the usage over the block", () => {
		defineBlocks("fr");
		const workspace = new Blockly.Workspace();
		const note = workspace.newBlock("sound_note");
		const field = note.getField("NOTE");
		expect(field?.getText()).toBe("do 4");
		expect(field?.getTooltip()).toBe("do 4 : 262 Hz");
		field?.setValue("440");
		expect(field?.getTooltip()).toBe("la 4 : 440 Hz — le la du diapason");
		expect(note.getTooltip()).toContain("à glisser dans un bloc du buzzer");
		workspace.dispose();
	});
});
