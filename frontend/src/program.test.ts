import { describe, expect, it } from "vitest";

import { isScript, isStarterCode, problemRange, programLanguages, programSwitch, starterCode } from "@/program";

describe("program languages", () => {
	it("lists the blocks first, then the text languages", () => {
		expect(programLanguages).toEqual(["blocks", "cpp", "python", "javascript"]);
		expect(isScript("python")).toBe(true);
		expect(isScript("javascript")).toBe(true);
		expect(isScript("cpp")).toBe(false);
		expect(isScript("blocks")).toBe(false);
	});

	it("starts every text language with Blink, commented in the user's language", () => {
		expect(starterCode("cpp", "fr")).toContain("void loop()");
		expect(starterCode("cpp", "fr")).toContain("Clignotant");
		expect(starterCode("python", "en")).toContain("while True:");
		expect(starterCode("python", "en")).toContain("Blink");
		expect(starterCode("javascript", "fr")).toContain("while (true) {");
		for (const program of ["cpp", "python", "javascript"] as const) {
			expect(starterCode(program, "en")).toContain("digitalWrite(LED_BUILTIN, HIGH)");
		}
	});

	it("knows when nothing typed would be lost", () => {
		expect(isStarterCode("")).toBe(true);
		expect(isStarterCode(`\n${starterCode("python", "fr")}\n`)).toBe(true);
		expect(isStarterCode(starterCode("javascript", "en"))).toBe(true);
		expect(isStarterCode(`${starterCode("cpp", "en")}// mine`)).toBe(false);
	});
});

describe("problemRange", () => {
	const code = "void setup() {\n  pinMod(13, OUTPUT);\n}\n";

	it("covers the line from the column", () => {
		expect(problemRange(code, { line: 2, column: 3, message: "" })).toEqual({ from: 17, to: 36 });
	});

	it("starts at the first character when the column is unknown", () => {
		expect(problemRange(code, { line: 2, message: "" })).toEqual({ from: 17, to: 36 });
		expect(problemRange(code, { line: 1, column: 0, message: "" })).toEqual({ from: 0, to: 14 });
	});

	it("stays inside the text for lines out of range", () => {
		const last = problemRange(code, { line: 99, message: "" });
		expect(last.from).toBeLessThanOrEqual(code.length);
		expect(problemRange(code, { line: 0, message: "" }).from).toBe(0);
	});

	it("converts a program into the new language, typed or not", () => {
		const sketch = "void setup() {\n  pinMode(13, OUTPUT);\n}\n\nvoid loop() {\n}\n";
		// C++ made from the blocks (not typed): converted without asking.
		expect(programSwitch("cpp", sketch, false)).toEqual({ action: "convert", confirm: false });
		// Typed code: converted after a yes.
		expect(programSwitch("python", "led = 13\n", true)).toEqual({ action: "convert", confirm: true });
		// An untouched example, or the blocks: the example of the new language.
		expect(programSwitch("cpp", starterCode("cpp", "fr"), true)).toEqual({ action: "start" });
		expect(programSwitch("blocks", "", false)).toEqual({ action: "start" });
	});
});
