import { describe, expect, it } from "vitest";

describe("references to variables and functions", () => {
	it("drops a variable field holding a function or an unknown id", async () => {
		const { emptyProject, projectToWorkspace } = await import("@/project/convert");
		const project: import("@/project/convert").Project = {
			...emptyProject("p", "uno"),
			variables: [{ id: "v1", name: "score", initialValue: 0 }],
			functions: [{ id: "f1", name: "flash" }],
			scripts: [
				{
					x: 0,
					y: 0,
					blocks: [
						{ id: "a", type: "variables_set", fields: { VARIABLE: "f1" } }, // a function: wrong kind
						{ id: "b", type: "variables_set", fields: { VARIABLE: "gone" } }, // unknown
						{ id: "c", type: "variables_set", fields: { VARIABLE: "v1" } },
						{ id: "d", type: "functions_call", fields: { FUNCTION: "v1" } }, // a variable: wrong kind
						{ id: "e", type: "functions_call", fields: { FUNCTION: "f1" } },
					],
				},
			],
		};
		const states: Record<string, unknown> = {};
		for (let block = projectToWorkspace(project).blocks?.blocks[0]; block; block = block.next?.block) {
			states[block.id ?? ""] = block.fields?.VARIABLE ?? block.fields?.FUNCTION;
		}
		expect(states).toEqual({ a: undefined, b: undefined, c: { id: "v1" }, d: undefined, e: { id: "f1" } });
	});
});

import { findBlock } from "@/catalog";
import allBlocks from "../../../examples/projects/all-blocks/project.json";
import blink from "../../../examples/projects/blink/project.json";
import trafficLight from "../../../examples/projects/traffic-light/project.json";
import distanceAlarm from "../../../examples/projects/distance-alarm/project.json";
import lightGauge from "../../../examples/projects/light-gauge/project.json";
import reactionGame from "../../../examples/projects/reaction-game/project.json";
import servoPotentiometer from "../../../examples/projects/servo-potentiometer/project.json";
import thermometer from "../../../examples/projects/thermometer/project.json";
import {
	isProject,
	projectToWorkspace,
	unknownBlockTypes,
	workspaceToProject,
	type Project,
	type ProjectBlock,
} from "@/project/convert";

function roundTrip(project: Project): Project {
	const state = projectToWorkspace(project);
	const initialValues = Object.fromEntries(project.variables.map((variable) => [variable.id, variable.initialValue]));
	return workspaceToProject(state, { name: project.name, board: project.board, initialValues });
}

/** A number input without a typed value means "the catalog default" (what the core generator uses). */
function withDefaults(block: ProjectBlock): ProjectBlock {
	const definition = findBlock(block.type);
	const result: ProjectBlock = { ...block };
	if (block.inputs) {
		result.inputs = Object.fromEntries(
			Object.entries(block.inputs).map(([name, input]) => {
				const arg = definition?.args?.find((candidate) => candidate.name === name);
				const value = input.value ?? (arg?.kind === "number" ? String(arg.default ?? 0) : undefined);
				return [
					name,
					{
						...(value !== undefined ? { value } : {}),
						...(input.block ? { block: withDefaults(input.block) } : {}),
					},
				];
			})
		);
	}
	if (block.statements) {
		result.statements = Object.fromEntries(
			Object.entries(block.statements).map(([name, stack]) => [name, stack.map(withDefaults)])
		);
	}
	return result;
}

function normalized(project: Project): Project {
	return {
		...project,
		scripts: project.scripts.map((script) => ({ ...script, blocks: script.blocks.map(withDefaults) })),
	};
}

describe("project conversion", () => {
	it("leaves out blocks this version does not know and keeps the rest of the script", () => {
		const project: Project = {
			...(blink as Project),
			scripts: [
				{
					x: 0,
					y: 0,
					blocks: [
						{ id: "hat", type: "events_when_program_starts" },
						{ id: "future", type: "future_block", inputs: { VALUE: { value: "1" } } },
						{
							id: "wait",
							type: "control_wait",
							inputs: { SECONDS: { value: "2", block: { id: "f2", type: "future_reporter" } } },
						},
						{ id: "led", type: "pins_builtin_led", fields: { STATE: "ON" } },
					],
				},
			],
		};
		expect(unknownBlockTypes(project)).toEqual(["future_block", "future_reporter"]);
		const back = roundTrip(project);
		expect(back.scripts).toHaveLength(1);
		expect(back.scripts[0]?.blocks.map((block) => block.id)).toEqual(["hat", "wait", "led"]);
		expect(back.scripts[0]?.blocks[1]?.inputs?.SECONDS).toEqual({ value: "2" });
	});

	it("knows every block of the examples", () => {
		expect(unknownBlockTypes(allBlocks as Project)).toEqual([]);
		expect(unknownBlockTypes(blink as Project)).toEqual([]);
	});

	it("moves blocks below a forever loop to a script of their own", () => {
		const project: Project = {
			...(blink as Project),
			scripts: [
				{
					x: 0,
					y: 0,
					blocks: [
						{ id: "start", type: "events_when_program_starts" },
						{ id: "loop", type: "control_forever" },
						{ id: "after", type: "pins_builtin_led", fields: { STATE: "OFF" } },
					],
				},
			],
		};
		const result = roundTrip(project);
		expect(result.scripts).toHaveLength(2);
		expect(result.scripts[0]?.blocks.map((block) => block.id)).toEqual(["start", "loop"]);
		expect(result.scripts[1]?.blocks.map((block) => block.id)).toEqual(["after"]);
	});

	it.each([
		["blink", blink],
		["all-blocks", allBlocks],
		["traffic-light", trafficLight],
		["distance-alarm", distanceAlarm],
		["servo-potentiometer", servoPotentiometer],
		["thermometer", thermometer],
		["reaction-game", reactionGame],
		["light-gauge", lightGauge],
	])("round-trips the %s example through the Blockly state", (_name, example) => {
		expect(isProject(example)).toBe(true);
		const project = example as Project;
		expect(roundTrip(project)).toEqual(normalized(project));
	});

	it("is stable over several round trips", () => {
		const once = roundTrip(allBlocks as Project);
		expect(roundTrip(once)).toEqual(once);
	});

	it("puts typed values in number shadows and chains statements with next", () => {
		const state = projectToWorkspace(blink as Project);
		const forever = state.blocks?.blocks[0]?.next?.block;
		expect(forever?.type).toBe("control_forever");
		const first = forever?.inputs?.SUBSTACK?.block;
		expect(first?.type).toBe("pins_builtin_led");
		const wait = first?.next?.block;
		expect(wait?.inputs?.SECONDS?.shadow).toEqual({ type: "math_number", fields: { NUM: "1" } });
	});

	it("maps variable fields to ids and keeps initial values", () => {
		const state = projectToWorkspace(allBlocks as Project);
		expect(state.variables).toContainEqual({ id: "lum", name: "luminosité", type: "" });
		const project = workspaceToProject(state, { name: "x", board: "uno", initialValues: { score: 5 } });
		expect(project.variables.find((variable) => variable.id === "score")?.initialValue).toBe(5);
		expect(project.scripts[0]?.blocks[1]?.fields).toEqual({ VARIABLE: "score" });
	});

	it("rejects files that are not projects", () => {
		expect(isProject(null)).toBe(false);
		expect(isProject({ format: "other" })).toBe(false);
	});
});
