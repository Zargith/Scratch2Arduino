import { describe, expect, it } from "vitest";

import { arduinoApi, completions, declaredNames } from "@/codeHelp";

describe("light completion", () => {
	it("offers the Arduino functions with a description in the user's language", () => {
		const cpp = completions("cpp", "fr", "");
		const write = cpp.find((option) => option.label === "digitalWrite");
		expect(write?.info).toContain("Allume");
		expect(cpp.some((option) => option.label === "Serial.println")).toBe(true);
		const python = completions("python", "en", "");
		expect(python.some((option) => option.label === "Serial.println")).toBe(false);
		expect(python.find((option) => option.label === "servoWrite")?.info).toContain("servo");
		for (const entry of arduinoApi) {
			expect(entry.info.fr).not.toBe("");
			expect(entry.info.en).not.toBe("");
		}
	});

	it("offers the keywords and snippets of the language", () => {
		const labels = completions("python", "fr", "").map((option) => option.label);
		expect(labels).toContain("def");
		expect(labels).toContain("setup / loop");
		expect(labels).not.toContain("let");
		expect(completions("javascript", "fr", "").map((option) => option.label)).toContain("let");
	});

	it("offers the names declared in the user's code", () => {
		expect(declaredNames("cpp", "int speed = 3;\nvoid blink(int n) {\n}\n")).toEqual([
			{ name: "blink", type: "function" },
			{ name: "speed", type: "variable" },
		]);
		expect(declaredNames("python", "speed = 3\ndef blink(n):\n    pass\n")).toEqual([
			{ name: "blink", type: "function" },
			{ name: "speed", type: "variable" },
		]);
		expect(declaredNames("javascript", "let speed = 3;\nfunction blink(n) {}\n").map((found) => found.name)).toEqual([
			"blink",
			"speed",
		]);
		const mine = completions("javascript", "en", "function blink() {}").find((option) => option.label === "blink");
		expect(mine?.detail).toBe("your code");
	});
});
