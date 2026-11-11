import { describe, expect, it } from "vitest";

import { myCodeContents, OPEN_CODE_CALLBACK, type UserDeclaration } from "@/blocks/definitions";

const declaration = (overrides: Partial<UserDeclaration>): UserDeclaration => ({
	kind: "function",
	name: "f",
	parameters: [],
	returnsValue: false,
	readOnly: false,
	line: 1,
	...overrides,
});

describe("My code palette", () => {
	it("invites to write code when there is none", () => {
		expect(myCodeContents([], "Write my code...")).toEqual([
			{ kind: "button", text: "Write my code...", callbackkey: OPEN_CODE_CALLBACK },
		]);
	});

	it("gives each function a call block, and a result block when it gives a value", () => {
		const contents = myCodeContents(
			[
				declaration({ name: "blink", parameters: ["times", "pause"] }),
				declaration({ name: "average", parameters: ["a", "b"], returnsValue: true }),
				declaration({ kind: "variable", name: "speed" }),
				declaration({ kind: "variable", name: "LIMIT", readOnly: true }),
			],
			""
		) as { type: string; fields?: { NAME: string }; extraState?: unknown; inputs?: object }[];
		expect(contents.map((block) => `${block.type}:${block.fields?.NAME}`)).toEqual([
			"mycode_call:blink",
			"mycode_call:average",
			"mycode_value:average",
			"mycode_get:speed",
			"mycode_set:speed",
			"mycode_get:LIMIT",
		]);
		expect(contents[0].extraState).toEqual({ name: "blink", params: 2 });
		expect(Object.keys(contents[0].inputs ?? {})).toEqual(["ARG1", "ARG2"]);
	});
});
