// The languages a program can be written in, and the code a new text program starts with.
// C++ is compiled and uploaded like the blocks; Python and JavaScript run on the computer and
// drive the board in Live mode (core/include/s2a/script.hpp lists their Arduino functions).
import type { Language } from "@/catalog";
import type { ProgramLanguage } from "@/project/convert";

export const programLanguages: ProgramLanguage[] = ["blocks", "cpp", "python", "javascript"];

/** Name shown in the language switch (the same in French and English, except the blocks). */
export const programNames: Record<Exclude<ProgramLanguage, "blocks">, string> = {
	cpp: "C++",
	python: "Python",
	javascript: "JavaScript",
};

/** True for the languages that run on the computer, in Live mode. */
export const isScript = (language: ProgramLanguage): boolean => language === "python" || language === "javascript";

const comments: Record<Language, { blink: string; loop: string; pin: string }> = {
	fr: {
		blink: "Clignotant : la LED de la carte s'allume et s'éteint chaque seconde.",
		loop: "Recommence encore et encore.",
		pin: "La broche de la LED devient une sortie.",
	},
	en: {
		blink: "Blink: the board's LED goes on and off every second.",
		loop: "Again and again.",
		pin: "The LED's pin becomes an output.",
	},
};

/** Blink, the first program of every learner, in `program` with comments in `language`. */
export function starterCode(program: Exclude<ProgramLanguage, "blocks">, language: Language): string {
	const c = comments[language];
	switch (program) {
		case "cpp":
			return [
				`// ${c.blink}`,
				"",
				"void setup() {",
				`  pinMode(LED_BUILTIN, OUTPUT); // ${c.pin}`,
				"}",
				"",
				`void loop() { // ${c.loop}`,
				"  digitalWrite(LED_BUILTIN, HIGH);",
				"  delay(1000);",
				"  digitalWrite(LED_BUILTIN, LOW);",
				"  delay(1000);",
				"}",
				"",
			].join("\n");
		case "python":
			return [
				`# ${c.blink}`,
				"",
				`pinMode(LED_BUILTIN, OUTPUT)  # ${c.pin}`,
				"",
				`while True:  # ${c.loop}`,
				"    digitalWrite(LED_BUILTIN, HIGH)",
				"    delay(1000)",
				"    digitalWrite(LED_BUILTIN, LOW)",
				"    delay(1000)",
				"",
			].join("\n");
		case "javascript":
			return [
				`// ${c.blink}`,
				"",
				`pinMode(LED_BUILTIN, OUTPUT); // ${c.pin}`,
				"",
				`while (true) { // ${c.loop}`,
				"  digitalWrite(LED_BUILTIN, HIGH);",
				"  delay(1000);",
				"  digitalWrite(LED_BUILTIN, LOW);",
				"  delay(1000);",
				"}",
				"",
			].join("\n");
	}
}

/** True when `code` is one of the starter programs (nothing typed by the user would be lost). */
export function isStarterCode(code: string): boolean {
	const trimmed = code.trim();
	if (trimmed === "") return true;
	return (["cpp", "python", "javascript"] as const).some((program) =>
		(["fr", "en"] as const).some((language) => starterCode(program, language).trim() === trimmed)
	);
}

/** A mistake at a line of the code, shown in the code editor after Check, Upload or Live. */
export interface CodeProblem {
	/** 1-based; 0 when unknown. */
	line: number;
	/** 1-based; 0 when unknown. */
	column?: number;
	message: string;
	/** "" for the program, "user" for the "My code" file. */
	file?: string;
}

/**
 * Where a problem goes in a text of `lines`: from the column (or the first non-blank
 * character) to the end of the line. Lines out of range go to the last line.
 */
export function problemRange(code: string, problem: CodeProblem): { from: number; to: number } {
	const lines = code.split("\n");
	const index = Math.min(Math.max(problem.line, 1), lines.length) - 1;
	let start = 0;
	for (let i = 0; i < index; i++) start += lines[i].length + 1;
	const text = lines[index];
	const firstChar = text.length - text.trimStart().length;
	const column = problem.column && problem.column > 0 ? Math.min(problem.column - 1, text.length) : firstChar;
	const from = start + Math.min(column, text.length);
	const to = start + text.length;
	return { from, to: Math.max(from, to) };
}

/** A first "My code" file: a variable and two functions, that become blocks at once. */
export function starterUserCode(program: Exclude<ProgramLanguage, "blocks">, language: Language): string {
	const fr = language === "fr";
	const [speed, blink, times, average] = fr ? ["vitesse", "clignoter", "fois", "moyenne"] : ["speed", "blink", "times", "average"];
	const intro = fr
		? "Mon code : les fonctions et les variables écrites ici deviennent des blocs « Mon code »."
		: "My code: the functions and variables written here become \"My code\" blocks.";
	switch (program) {
		case "cpp":
			return [
				`// ${intro}`,
				`int ${speed} = 200;`,
				"",
				`void ${blink}(int ${times}) {`,
				`  for (int i = 0; i < ${times}; i++) {`,
				"    digitalWrite(LED_BUILTIN, HIGH);",
				`    delay(${speed});`,
				"    digitalWrite(LED_BUILTIN, LOW);",
				`    delay(${speed});`,
				"  }",
				"}",
				"",
				`float ${average}(float a, float b) {`,
				"  return (a + b) / 2;",
				"}",
				"",
			].join("\n");
		case "python":
			return [
				`# ${intro}`,
				`${speed} = 200`,
				"",
				`def ${blink}(${times}):`,
				`    for i in range(int(${times})):`,
				"        digitalWrite(LED_BUILTIN, HIGH)",
				`        delay(${speed})`,
				"        digitalWrite(LED_BUILTIN, LOW)",
				`        delay(${speed})`,
				"",
				`def ${average}(a, b):`,
				"    return (a + b) / 2",
				"",
			].join("\n");
		case "javascript":
			return [
				`// ${intro}`,
				`let ${speed} = 200;`,
				"",
				`function ${blink}(${times}) {`,
				`  for (let i = 0; i < ${times}; i++) {`,
				"    digitalWrite(LED_BUILTIN, HIGH);",
				`    delay(${speed});`,
				"    digitalWrite(LED_BUILTIN, LOW);",
				`    delay(${speed});`,
				"  }",
				"}",
				"",
				`function ${average}(a, b) {`,
				"  return (a + b) / 2;",
				"}",
				"",
			].join("\n");
	}
}

/** True when `code` is a first "My code" file, unchanged (switching its language loses nothing). */
export function isStarterUserCode(code: string): boolean {
	const trimmed = code.trim();
	return (["cpp", "python", "javascript"] as const).some((program) =>
		(["fr", "en"] as const).some((language) => starterUserCode(program, language).trim() === trimmed)
	);
}
