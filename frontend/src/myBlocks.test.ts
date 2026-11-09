import * as Blockly from "scratch-blocks";
import { describe, expect, it } from "vitest";

import { defineBlocks } from "@/blocks/definitions";

import { countBlocks, groupToBlocks, stackToGroup } from "@/myBlocks";
import type { BlockState } from "@/project/convert";

/** "set score to 0", then "call flash", then "repeat 3 { change score by 1 }". */
const stack: BlockState = {
	type: "variables_set",
	id: "b1",
	fields: { VARIABLE: { id: "v-score" } },
	inputs: { VALUE: { shadow: { type: "text", fields: { TEXT: "0" } } } },
	next: {
		block: {
			type: "functions_call",
			id: "b2",
			fields: { FUNCTION: { id: "f-flash" } },
			next: {
				block: {
					type: "control_repeat",
					id: "b3",
					inputs: {
						TIMES: { shadow: { type: "math_whole_number", fields: { NUM: "3" } } },
						SUBSTACK: {
							block: {
								type: "variables_change",
								id: "b4",
								fields: { VARIABLE: { id: "v-score" } },
								inputs: { VALUE: { shadow: { type: "math_number", fields: { NUM: "1" } } } },
							},
						},
					},
				},
			},
		},
	},
};

const workspaceVariables = [
	{ id: "v-score", name: "score", type: "" },
	{ id: "v-unused", name: "lumière", type: "" },
	{ id: "f-flash", name: "flash", type: "function" },
	{ id: "f-unused", name: "autre", type: "function" },
];

let counter = 0;
const newId = () => `new-${++counter}`;

describe("My blocks", () => {
	it("saves a stack with only the variables and functions it uses", () => {
		const group = stackToGroup(stack, workspaceVariables);
		expect(group.scripts).toHaveLength(1);
		expect(group.scripts[0]?.blocks.map((block) => block.type)).toEqual([
			"variables_set",
			"functions_call",
			"control_repeat",
		]);
		expect(group.variables).toEqual([{ id: "v-score", name: "score", initialValue: 0 }]);
		expect(group.functions).toEqual([{ id: "f-flash", name: "flash" }]);
		expect(countBlocks(group)).toBe(4);
	});

	it("leaves out the functions list when no function is used", () => {
		const alone: BlockState = { type: "control_wait", id: "w", inputs: { SECONDS: { shadow: { type: "math_positive_number", fields: { NUM: "1" } } } } };
		const group = stackToGroup(alone, workspaceVariables);
		expect(group.variables).toEqual([]);
		expect(group.functions).toBeUndefined();
	});

	it("reuses the variables of the project by name and creates the missing ones", () => {
		const group = stackToGroup(stack, workspaceVariables);
		// Another project: it has a "score" variable (other id) but no "flash" function.
		const insertion = groupToBlocks(group, [{ id: "other-score", name: "score", type: "" }], newId);
		expect(insertion.create).toEqual([{ id: expect.stringMatching(/^new-/), name: "flash", type: "function" }]);
		const flash = insertion.create[0]?.id;

		const top = insertion.blocks[0];
		expect(insertion.blocks).toHaveLength(1);
		expect(top?.x).toBeUndefined();
		expect(top?.fields?.VARIABLE).toEqual({ id: "other-score" });
		const call = top?.next?.block;
		expect(call?.fields?.FUNCTION).toEqual({ id: flash });
		const inner = call?.next?.block?.inputs?.SUBSTACK?.block;
		expect(inner?.fields?.VARIABLE).toEqual({ id: "other-score" });

		// Fresh block ids, so the group can be added several times.
		const ids = [top?.id, call?.id, call?.next?.block?.id, inner?.id];
		expect(new Set(ids).size).toBe(4);
		expect(ids).not.toContain("b1");
	});

	it("does not mix a variable and a function with the same name", () => {
		const group = stackToGroup(stack, workspaceVariables);
		const insertion = groupToBlocks(group, [{ id: "fn-score", name: "score", type: "function" }], newId);
		expect(insertion.create.map((created) => `${created.type}:${created.name}`).sort()).toEqual([":score", "function:flash"]);
	});

	it("round-trips through a real Blockly workspace", () => {
		Blockly.ScratchMsgs.setLocale("fr");
		defineBlocks("fr");
		// Project A: the stack is saved from here.
		const source = new Blockly.Workspace();
		for (const variable of workspaceVariables) source.getVariableMap().createVariable(variable.name, variable.type, variable.id);
		const top = Blockly.serialization.blocks.append(stack as unknown as Parameters<typeof Blockly.serialization.blocks.append>[0], source);
		const saved = Blockly.serialization.blocks.save(top, { addCoordinates: false, addNextBlocks: true });
		const variables = source
			.getVariableMap()
			.getAllVariables()
			.map((variable) => ({ id: variable.getId(), name: variable.getName(), type: variable.getType() }));
		const group = stackToGroup(saved as unknown as BlockState, variables);
		source.dispose();

		// Project B: has its own "score"; the group is added twice.
		const target = new Blockly.Workspace();
		target.getVariableMap().createVariable("score", "", "b-score");
		for (let round = 0; round < 2; round++) {
			// As the editor's insertGroup does.
			const existing = target
				.getVariableMap()
				.getAllVariables()
				.map((variable) => ({ id: variable.getId(), name: variable.getName(), type: variable.getType() }));
			const insertion = groupToBlocks(group, existing, () => Blockly.utils.idGenerator.genUid());
			expect(insertion.create).toHaveLength(round === 0 ? 1 : 0);
			for (const created of insertion.create) target.getVariableMap().createVariable(created.name, created.type, created.id);
			for (const state of insertion.blocks) {
				Blockly.serialization.blocks.append(state as unknown as Parameters<typeof Blockly.serialization.blocks.append>[0], target);
			}
		}
		expect(target.getAllBlocks(false).filter((block) => block.type === "variables_set")).toHaveLength(2);
		expect(target.getVariableMap().getAllVariables().map((variable) => `${variable.getType()}:${variable.getName()}`).sort()).toEqual([
			":score",
			"function:flash",
		]);
		for (const block of target.getAllBlocks(false).filter((candidate) => candidate.type === "variables_set")) {
			expect(block.getFieldValue("VARIABLE")).toBe("b-score");
		}
		target.dispose();
	});

	it("drops references the group does not describe", () => {
		const group = stackToGroup(stack, workspaceVariables);
		group.variables = []; // damaged file
		const insertion = groupToBlocks(group, [], newId);
		expect(insertion.blocks[0]?.fields?.VARIABLE).toBeUndefined();
		expect(insertion.blocks[0]?.type).toBe("variables_set");
	});
});
