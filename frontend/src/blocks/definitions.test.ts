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

	it("speaks of the function, not of a variable, in the function menu", () => {
		Blockly.ScratchMsgs.setLocale("fr"); // as the editor does: the "delete" entry needs it
		defineBlocks("fr");
		const workspace = new Blockly.Workspace();
		workspace.getVariableMap().createVariable("clignoter", "function", "f1");
		const define = workspace.newBlock("functions_define");
		const field = define.getField("FUNCTION") as Blockly.FieldDropdown;
		field.setValue("f1");
		const labels = field.getOptions(false).map((option) => (option as unknown[])[0]);
		expect(labels).toContain("Renommer la fonction");
		expect(labels).toContain("Supprimer la fonction « clignoter »");
		expect(labels.some((label) => String(label).includes("variable"))).toBe(false);
		workspace.dispose();
	});
});
