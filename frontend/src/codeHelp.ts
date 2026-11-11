// Light autocompletion of the code editor: the language keywords, the Arduino functions (with a
// short fr/en description), the names declared in the user's file and a few snippets. No language
// server: everything is a list in this file, bundled offline.
import { snippetCompletion, type Completion, type CompletionContext, type CompletionResult } from "@codemirror/autocomplete";

import type { Language } from "@/catalog";
import type { ProgramLanguage } from "@/project/convert";

export type TextLanguage = Exclude<ProgramLanguage, "blocks">;

interface ApiEntry {
	label: string;
	/** Signature shown beside the name. */
	detail: string;
	info: Record<Language, string>;
	type: "function" | "constant" | "variable";
	/** Languages that have it; all when left out. */
	languages?: TextLanguage[];
}

/** The Arduino functions and constants, shared by C++ and the arduino API of Python and JavaScript. */
export const arduinoApi: ApiEntry[] = [
	{ label: "pinMode", detail: "(pin, mode)", type: "function", info: { fr: "Choisit si une broche est une sortie (OUTPUT) ou une entrée (INPUT, INPUT_PULLUP).", en: "Makes a pin an output (OUTPUT) or an input (INPUT, INPUT_PULLUP)." } },
	{ label: "digitalWrite", detail: "(pin, HIGH | LOW)", type: "function", info: { fr: "Allume (HIGH) ou éteint (LOW) une broche de sortie.", en: "Turns an output pin on (HIGH) or off (LOW)." } },
	{ label: "digitalRead", detail: "(pin) -> HIGH | LOW", type: "function", info: { fr: "Lit une broche d'entrée : HIGH (5 V) ou LOW (0 V).", en: "Reads an input pin: HIGH (5 V) or LOW (0 V)." } },
	{ label: "analogRead", detail: "(A0) -> 0...1023", type: "function", info: { fr: "Lit une tension sur une broche analogique : de 0 (0 V) à 1023 (5 V).", en: "Reads a voltage on an analog pin: from 0 (0 V) to 1023 (5 V)." } },
	{ label: "analogWrite", detail: "(pin ~, 0...255)", type: "function", info: { fr: "Règle une broche PWM (~) de 0 (éteint) à 255 (à fond) : luminosité, vitesse.", en: "Sets a PWM pin (~) from 0 (off) to 255 (full): brightness, speed." } },
	{ label: "delay", detail: "(milliseconds)", type: "function", info: { fr: "Attend un moment, en millisecondes (1000 = 1 seconde).", en: "Waits a while, in milliseconds (1000 = 1 second)." } },
	{ label: "millis", detail: "() -> ms", type: "function", info: { fr: "Le temps écoulé depuis le démarrage, en millisecondes.", en: "Time since the start, in milliseconds." } },
	{ label: "tone", detail: "(pin, frequency, duration?)", type: "function", info: { fr: "Joue une note sur un buzzer (fréquence en Hz, durée en ms).", en: "Plays a note on a buzzer (frequency in Hz, duration in ms)." } },
	{ label: "noTone", detail: "(pin)", type: "function", info: { fr: "Arrête le son du buzzer.", en: "Stops the buzzer's sound." } },
	{ label: "map", detail: "(value, fromLow, fromHigh, toLow, toHigh)", type: "function", languages: ["cpp"], info: { fr: "Change l'échelle d'un nombre (par exemple 0...1023 vers 0...180).", en: "Rescales a number (for example 0...1023 to 0...180)." } },
	{ label: "random", detail: "(min, max)", type: "function", languages: ["cpp"], info: { fr: "Un nombre au hasard, de min à max - 1.", en: "A random number, from min to max - 1." } },
	{ label: "Serial.begin", detail: "(9600)", type: "function", languages: ["cpp"], info: { fr: "Ouvre le moniteur série à cette vitesse (dans setup).", en: "Opens the serial monitor at this speed (in setup)." } },
	{ label: "Serial.println", detail: "(value)", type: "function", languages: ["cpp"], info: { fr: "Écrit une ligne sur le moniteur série.", en: "Writes a line to the serial monitor." } },
	{ label: "Serial.print", detail: "(value)", type: "function", languages: ["cpp"], info: { fr: "Écrit sur le moniteur série, sans aller à la ligne.", en: "Writes to the serial monitor, without a new line." } },
	{ label: "servoWrite", detail: "(pin, angle)", type: "function", languages: ["python", "javascript"], info: { fr: "Tourne un servomoteur à un angle de 0 à 180°.", en: "Turns a servo to an angle from 0 to 180°." } },
	{ label: "print", detail: "(...)", type: "function", languages: ["python", "javascript"], info: { fr: "Écrit sur le moniteur série et dans le panneau Live.", en: "Writes to the serial monitor and the Live panel." } },
	{ label: "console.log", detail: "(...)", type: "function", languages: ["javascript"], info: { fr: "Comme print : écrit dans le panneau Live.", en: "Like print: writes to the Live panel." } },
	{ label: "HIGH", detail: "1", type: "constant", info: { fr: "Allumé, 5 V.", en: "On, 5 V." } },
	{ label: "LOW", detail: "0", type: "constant", info: { fr: "Éteint, 0 V.", en: "Off, 0 V." } },
	{ label: "OUTPUT", detail: "pinMode", type: "constant", info: { fr: "Broche de sortie (LED, buzzer, moteur).", en: "Output pin (LED, buzzer, motor)." } },
	{ label: "INPUT", detail: "pinMode", type: "constant", info: { fr: "Broche d'entrée (capteur).", en: "Input pin (sensor)." } },
	{ label: "INPUT_PULLUP", detail: "pinMode", type: "constant", info: { fr: "Entrée avec résistance interne : un bouton relié au GND donne LOW quand on appuie.", en: "Input with the internal resistor: a button wired to GND reads LOW when pressed." } },
	{ label: "LED_BUILTIN", detail: "13", type: "constant", info: { fr: "La broche de la LED de la carte.", en: "The pin of the board's LED." } },
	{ label: "A0", detail: "analog pin", type: "constant", info: { fr: "Broche analogique A0 (A1, A2... aussi).", en: "Analog pin A0 (A1, A2... too)." } },
];

const keywords: Record<TextLanguage, string[]> = {
	cpp: ["void", "int", "long", "float", "double", "bool", "byte", "char", "unsigned", "const", "static", "if", "else", "for", "while", "do", "switch", "case", "break", "continue", "return", "true", "false", "#include", "#define"],
	python: ["def", "return", "if", "elif", "else", "for", "in", "range", "while", "break", "continue", "pass", "and", "or", "not", "True", "False", "None", "global", "import"],
	javascript: ["function", "return", "let", "const", "var", "if", "else", "for", "while", "do", "break", "continue", "switch", "case", "true", "false", "null", "undefined"],
};

const snippets: Record<TextLanguage, { label: string; template: string; info: Record<Language, string> }[]> = {
	cpp: [
		{ label: "setup / loop", template: "void setup() {\n\t${}\n}\n\nvoid loop() {\n\t\n}", info: { fr: "Le squelette d'un programme Arduino.", en: "The frame of an Arduino program." } },
		{ label: "for", template: "for (int ${i} = 0; ${i} < ${10}; ${i}++) {\n\t${}\n}", info: { fr: "Répète un nombre de fois.", en: "Repeats a number of times." } },
		{ label: "if / else", template: "if (${condition}) {\n\t${}\n} else {\n\t\n}", info: { fr: "Si... sinon...", en: "If... else..." } },
		{ label: "while", template: "while (${condition}) {\n\t${}\n}", info: { fr: "Tant que...", en: "While..." } },
		{ label: "function", template: "void ${name}() {\n\t${}\n}", info: { fr: "Une fonction, utilisable dans les blocs \"Mon code\".", en: "A function, usable in the \"My code\" blocks." } },
	],
	python: [
		{ label: "setup / loop", template: "def setup():\n\t${pass}\n\ndef loop():\n\t", info: { fr: "Comme sur Arduino : setup() une fois, puis loop() sans arrêt.", en: "As on an Arduino: setup() once, then loop() again and again." } },
		{ label: "for", template: "for ${i} in range(${10}):\n\t${}", info: { fr: "Répète un nombre de fois.", en: "Repeats a number of times." } },
		{ label: "if / else", template: "if ${condition}:\n\t${}\nelse:\n\t", info: { fr: "Si... sinon...", en: "If... else..." } },
		{ label: "while", template: "while ${condition}:\n\t${}", info: { fr: "Tant que...", en: "While..." } },
		{ label: "function", template: "def ${name}():\n\t${pass}", info: { fr: "Une fonction, utilisable dans les blocs \"Mon code\".", en: "A function, usable in the \"My code\" blocks." } },
	],
	javascript: [
		{ label: "setup / loop", template: "function setup() {\n\t${}\n}\n\nfunction loop() {\n\t\n}", info: { fr: "Comme sur Arduino : setup() une fois, puis loop() sans arrêt.", en: "As on an Arduino: setup() once, then loop() again and again." } },
		{ label: "for", template: "for (let ${i} = 0; ${i} < ${10}; ${i}++) {\n\t${}\n}", info: { fr: "Répète un nombre de fois.", en: "Repeats a number of times." } },
		{ label: "if / else", template: "if (${condition}) {\n\t${}\n} else {\n\t\n}", info: { fr: "Si... sinon...", en: "If... else..." } },
		{ label: "while", template: "while (${condition}) {\n\t${}\n}", info: { fr: "Tant que...", en: "While..." } },
		{ label: "function", template: "function ${name}() {\n\t${}\n}", info: { fr: "Une fonction, utilisable dans les blocs \"Mon code\".", en: "A function, usable in the \"My code\" blocks." } },
	],
};

/**
 * Names declared in the code (functions and variables), for completion only: a few regular
 * expressions, the core's scanner makes the "My code" blocks.
 */
export function declaredNames(language: TextLanguage, code: string): { name: string; type: "function" | "variable" }[] {
	const patterns: Record<TextLanguage, [RegExp, "function" | "variable"][]> = {
		cpp: [
			[/^[ \t]*(?:static\s+|inline\s+|const\s+|volatile\s+|unsigned\s+)*(?:void|int|long|float|double|bool|boolean|byte|char|word|String)\s+([A-Za-z_]\w*)\s*\(/gm, "function"],
			[/^[ \t]*(?:static\s+|const\s+|volatile\s+|unsigned\s+)*(?:int|long|float|double|bool|boolean|byte|char|word|String)\s+([A-Za-z_]\w*)\s*(?:=|;|,|\[)/gm, "variable"],
		],
		python: [
			[/^[ \t]*def\s+([A-Za-z_]\w*)/gm, "function"],
			[/^[ \t]*([A-Za-z_]\w*)\s*=(?!=)/gm, "variable"],
		],
		javascript: [
			[/\bfunction\s+([A-Za-z_$][\w$]*)/g, "function"],
			[/\b(?:let|const|var)\s+([A-Za-z_$][\w$]*)/g, "variable"],
		],
	};
	const found = new Map<string, "function" | "variable">();
	for (const [pattern, type] of patterns[language]) {
		for (const match of code.matchAll(pattern)) if (!found.has(match[1])) found.set(match[1], type);
	}
	return [...found].map(([name, type]) => ({ name, type }));
}

/** Every completion offered in `language`, the user's names included. */
export function completions(language: TextLanguage, uiLanguage: Language, code: string): Completion[] {
	const api = arduinoApi
		.filter((entry) => !entry.languages || entry.languages.includes(language))
		.map((entry): Completion => ({
			label: entry.label,
			type: entry.type,
			detail: entry.detail,
			info: entry.info[uiLanguage],
			boost: 2,
		}));
	const words = keywords[language].map((word): Completion => ({ label: word, type: "keyword" }));
	const mine = declaredNames(language, code).map((declared): Completion => ({
		label: declared.name,
		type: declared.type,
		detail: uiLanguage === "fr" ? "ton code" : "your code",
		boost: 3,
	}));
	const templates = snippets[language].map((snippet) =>
		snippetCompletion(snippet.template, { label: snippet.label, type: "text", info: snippet.info[uiLanguage], boost: -1 })
	);
	return [...mine, ...api, ...words, ...templates];
}

/** CodeMirror completion source: the word before the cursor ("Serial.pr" included). */
export function completionSource(language: TextLanguage, uiLanguage: () => Language) {
	return (context: CompletionContext): CompletionResult | null => {
		const word = context.matchBefore(/[#\w$.]+/);
		if (!word || (word.from === word.to && !context.explicit)) return null;
		return {
			from: word.from,
			options: completions(language, uiLanguage(), context.state.doc.toString()),
			validFor: /^[#\w$.]*$/,
		};
	};
}
