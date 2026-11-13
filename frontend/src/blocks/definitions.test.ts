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
		expect(field?.getTooltip()).toBe("la 4 : 440 Hz - le la du diapason");
		expect(note.getTooltip()).toContain("à glisser dans un bloc du buzzer");
		workspace.dispose();
	});

	it("lists only variables, never functions, in the variable menu", () => {
		Blockly.ScratchMsgs.setLocale("fr");
		defineBlocks("fr");
		const workspace = new Blockly.Workspace();
		workspace.getVariableMap().createVariable("score", "", "v1");
		workspace.getVariableMap().createVariable("clignoter", "function", "f1");
		const set = workspace.newBlock("variables_set");
		const field = set.getField("VARIABLE") as Blockly.FieldDropdown;
		field.setValue("v1");
		const labels = field.getOptions(false).map((option) => String((option as unknown[])[0]));
		expect(labels).toContain("score");
		expect(labels).not.toContain("clignoter");
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
		expect(labels).toContain("Modifier la fonction");
		expect(labels).toContain("Supprimer la fonction \"clignoter\"");
		expect(labels.some((label) => String(label).includes("variable"))).toBe(false);
		workspace.dispose();
	});
	// Regression: the palette recycled "My code" variable blocks when it was rebuilt, keeping their
	// old label ("lampe" showed "avant") and leaving spare ones over "Events". Blockly never
	// recycles a block with extra state.
	it("never lets the palette recycle the My code variable blocks", () => {
		defineBlocks("fr");
		const workspace = new Blockly.Workspace();
		for (const type of ["mycode_get", "mycode_set", "mycode_call", "mycode_value"]) {
			const block = workspace.newBlock(type) as Blockly.Block & { saveExtraState?: unknown; loadExtraState?: unknown };
			expect(typeof block.saveExtraState, type).toBe("function");
			expect(typeof block.loadExtraState, type).toBe("function");
		}
		const get = workspace.newBlock("mycode_get");
		get.setFieldValue("lampe", "NAME");
		const saved = Blockly.serialization.blocks.save(get) as { fields?: { NAME?: string } };
		expect(saved.fields?.NAME).toBe("lampe"); // the name is still saved with the project
		workspace.dispose();
	});
});
