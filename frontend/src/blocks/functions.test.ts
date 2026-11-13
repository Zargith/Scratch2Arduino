import * as Blockly from "scratch-blocks";
import { describe, expect, it } from "vitest";

import { blockJson, defineBlocks, toolbox } from "@/blocks/definitions";
import {
	changeFunctionParameters,
	cleanParameterNames,
	functionParameters,
	registerParametersSerializer,
	setFunctionParameters,
	syncFunctionBlock,
} from "@/blocks/functions";
import { findBlock } from "@/catalog";
import { editedInputs } from "@/FunctionDialog";
import { projectToWorkspace, workspaceToProject, type Project } from "@/project/convert";

function workspaceWithFunction(inputs: string[]): Blockly.Workspace {
	Blockly.ScratchMsgs.setLocale("fr");
	defineBlocks("fr");
	registerParametersSerializer();
	const workspace = new Blockly.Workspace();
	workspace.getVariableMap().createVariable("biper", "function", "f1");
	setFunctionParameters("f1", inputs);
	return workspace;
}

function slots(block: Blockly.Block): string[] {
	return block.inputList.filter((input) => /^ARG\d$/.test(input.name)).map((input) => input.fieldRow.map((field) => field.getText()).join(""));
}

describe("functions with inputs", () => {
	it("cleans the names typed in the function window", () => {
		expect(cleanParameterNames([" fois ", "", "durée", "fois", "a", "b"])).toEqual(["fois", "durée", "fois 2", "a"]);
		expect(editedInputs([{ name: "x", from: 1 }, { name: " ", from: 0 }, { name: "y", from: null }])).toEqual([
			{ name: "x", from: 1 },
			{ name: "y", from: null },
		]);
	});

	it("gives a call block one slot per input, on its own line when there are several", () => {
		const workspace = workspaceWithFunction(["fois", "durée"]);
		const call = workspace.newBlock("functions_call");
		call.setFieldValue("f1", "FUNCTION");
		expect(syncFunctionBlock(call)).toBe(true);
		expect(slots(call)).toEqual(["fois", "durée"]);
		// An end-of-row input before each slot: the block grows taller, not wider.
		const names = call.inputList.map((input) => input.name);
		expect(names.indexOf("S2A_BREAK0")).toBe(names.indexOf("ARG1") - 1);
		expect(names.indexOf("S2A_BREAK1")).toBe(names.indexOf("ARG2") - 1);
		expect(call.getInput("ARG1")?.connection?.targetBlock()?.type).toBe("math_number");

		setFunctionParameters("f1", ["fois"]);
		syncFunctionBlock(call);
		expect(slots(call)).toEqual(["fois"]);
		expect(call.inputList.some((input) => input.name.startsWith("S2A_BREAK"))).toBe(false);
		workspace.dispose();
	});

	it("shows the names of the inputs on the define block", () => {
		const workspace = workspaceWithFunction(["fois", "durée"]);
		const define = workspace.newBlock("functions_define");
		define.setFieldValue("f1", "FUNCTION");
		syncFunctionBlock(define);
		expect(define.getInput("S2A_NAMES")?.fieldRow[0]?.getText()).toBe("entrées : fois, durée");
		workspace.dispose();
	});

	it("keeps what the slots hold when inputs are reordered, renamed or removed", () => {
		const workspace = workspaceWithFunction(["fois", "durée"]);
		const define = workspace.newBlock("functions_define");
		define.setFieldValue("f1", "FUNCTION");
		syncFunctionBlock(define);
		const wait = workspace.newBlock("control_wait");
		define.nextConnection?.connect(wait.previousConnection!);
		const input = workspace.newBlock("functions_parameter");
		input.setFieldValue("durée", "NAME");
		wait.getInput("SECONDS")?.connection?.connect(input.outputConnection!);
		const call = workspace.newBlock("functions_call");
		call.setFieldValue("f1", "FUNCTION");
		syncFunctionBlock(call);
		const knob = workspace.newBlock("sensors_potentiometer");
		call.getInput("ARG2")?.connection?.connect(knob.outputConnection!);

		// "durée" becomes "temps" and moves first, "fois" goes, "note" is new.
		changeFunctionParameters(workspace, "f1", [
			{ name: "temps", from: 1 },
			{ name: "note", from: null },
		]);
		expect(functionParameters("f1")).toEqual(["temps", "note"]);
		expect(slots(call)).toEqual(["temps", "note"]);
		expect(call.getInput("ARG1")?.connection?.targetBlock()).toBe(knob);
		expect(call.getInput("ARG2")?.connection?.targetBlock()?.type).toBe("math_number");
		expect(input.getFieldValue("NAME")).toBe("temps");
		workspace.dispose();
	});

	it("saves the inputs with the workspace and reads them back", () => {
		const workspace = workspaceWithFunction(["fois"]);
		const saved = Blockly.serialization.workspaces.save(workspace) as { functionParameters?: unknown };
		expect(saved.functionParameters).toEqual({ f1: ["fois"] });
		setFunctionParameters("f1", []);
		const other = new Blockly.Workspace();
		Blockly.serialization.workspaces.load(saved as never, other);
		expect(functionParameters("f1")).toEqual(["fois"]);
		workspace.dispose();
		other.dispose();
	});

	it("goes from project.json to the editor and back with the inputs", () => {
		const project: Project = {
			format: "scratch2arduino-project",
			formatVersion: 1,
			name: "Bips",
			board: "uno",
			variables: [],
			functions: [{ id: "f1", name: "biper", parameters: ["fois", "durée"] }],
			scripts: [
				{
					x: 0,
					y: 0,
					blocks: [
						{ id: "h", type: "events_when_program_starts" },
						{ id: "c", type: "functions_call", fields: { FUNCTION: "f1" }, inputs: { ARG1: { value: "3" }, ARG2: { value: "0.5" }, ARG4: { value: "9" } } },
					],
				},
			],
		};
		const state = projectToWorkspace(project);
		const call = state.blocks?.blocks[0]?.next?.block;
		expect(call?.extraState).toEqual({ params: ["fois", "durée"] });
		expect(Object.keys(call?.inputs ?? {})).toEqual(["ARG1", "ARG2"]); // ARG4: no such input any more
		expect(state.functionParameters).toEqual({ f1: ["fois", "durée"] });
		const back = workspaceToProject(state, { name: "Bips", board: "uno" });
		expect(back.functions).toEqual(project.functions);
		expect(back.scripts[0]?.blocks[1]?.inputs).toEqual({ ARG1: { value: "3" }, ARG2: { value: "0.5" } });
	});

	it("lists the input blocks of each function in the palette", () => {
		setFunctionParameters("f1", ["fois"]);
		setFunctionParameters("f2", []);
		const palette = toolbox("fr", [], { createVariable: "", createFunction: "+", inputsOf: (name) => `Entrées de ${name} :` }, [
			{ id: "f1", name: "biper" },
			{ id: "f2", name: "attendre" },
		]) as { contents: { name: string; contents: { kind: string; type?: string; text?: string; extraState?: unknown }[] }[] };
		const functions = palette.contents.find((category) => category.name === "Fonctions")?.contents ?? [];
		expect(functions.map((item) => item.type ?? item.text)).toEqual([
			"+",
			"functions_define",
			"functions_call",
			"Entrées de biper :",
			"functions_parameter",
		]);
		expect(functions[2]?.extraState).toEqual({ params: ["fois"] });
	});
});

describe("tall blocks", () => {
	it("breaks the line where the catalog message has one", () => {
		const block = findBlock("motors_dc_run");
		expect(block).toBeDefined();
		const json = blockJson(block!, "fr");
		expect(json.inputsInline).toBe(true);
		const args = json.args0 as { type: string }[];
		expect(args.filter((arg) => arg.type === "input_end_row")).toHaveLength(1);
		expect(String(json.message0)).toMatch(/^faire tourner le moteur %1 à la vitesse %2 ?%3 broches EN %4/);
	});
});
