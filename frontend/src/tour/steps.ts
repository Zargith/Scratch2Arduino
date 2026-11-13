// Interactive tutorial "Ma sonnette" (my doorbell): the steps, what each one points at, and how
// it sees by itself that the learner did it. A button on pin 2 lights an LED on pin 8 and rings a
// buzzer on pin 6; then a little C++ in "My code" plays a tune, converted to Python for Live mode.
import type { Language, Localized } from "@/catalog";
import type { Project, ProjectBlock, UserCode } from "@/project/convert";

/** What the editor shows now: read by the steps to know when they are done. */
export interface TourContext {
	project: Project;
	view: "blocks" | "code";
	wiringShown: boolean;
	live: boolean;
	/** The virtual board is the chosen port. */
	simulated: boolean;
	/** Pins of the virtual board whose button is held down now. */
	pressed: string[];
	userCode: UserCode | null;
	/** Successful "Check" (or upload) since the editor started: grows by one each time. */
	checks: number;
}

/** aria-labels of the editor the tutorial points at (keys of i18n.ts). */
export type TargetLabel = "toolbar.wiring" | "toolbar.live" | "toolbar.build" | "view.label" | "code.mine_language";

/** Where a step points: a palette category, a control (by its label) or a button by its text. */
export type TourTarget =
	| { kind: "category"; id: string }
	| { kind: "label"; key: TargetLabel }
	| { kind: "text"; text: Localized };

export interface TourStep {
	id: string;
	title: Localized;
	text: Localized;
	target?: TourTarget;
	/** A button of the step's card that does a part of the work (writing the tune). */
	action?: { label: Localized; run: "insert-melody" };
	/**
	 * True once the step is done. `memory` belongs to the step while it is shown (what was
	 * seen since it started); `start` is the context when the step started.
	 */
	done(context: TourContext, memory: Record<string, unknown>, start: TourContext): boolean;
}

// --- reading the project ------------------------------------------------------------------

/** Every block of a stack, with the blocks inside (slots and mouths). */
function* allBlocks(stack: ProjectBlock[]): Generator<ProjectBlock> {
	for (const block of stack) {
		yield block;
		for (const input of Object.values(block.inputs ?? {})) if (input.block) yield* allBlocks([input.block]);
		for (const inner of Object.values(block.statements ?? {})) yield* allBlocks(inner);
	}
}

/** The script that starts with "when the program starts" (the first one). */
export function mainScript(project: Project): ProjectBlock[] {
	return project.scripts.find((script) => script.blocks[0]?.type === "events_when_program_starts")?.blocks ?? [];
}

/** The "forever" block attached under the start block. */
const forever = (project: Project) => mainScript(project).find((block) => block.type === "control_forever");

/** The "if ... else" block inside "forever". */
const ifElse = (project: Project) =>
	[...allBlocks(forever(project)?.statements?.SUBSTACK ?? [])].find((block) => block.type === "control_if_else");

const field = (block: ProjectBlock | undefined, name: string) => block?.fields?.[name] ?? "";

/** A block of `type` somewhere in `stack` that satisfies `test`. */
const has = (stack: ProjectBlock[] | undefined, type: string, test: (block: ProjectBlock) => boolean = () => true) =>
	[...allBlocks(stack ?? [])].some((block) => block.type === type && test(block));

/** The LED block of a mouth: "set pin 8 to HIGH" (or LOW). */
const ledBlock = (stack: ProjectBlock[] | undefined, state: "HIGH" | "LOW") =>
	has(stack, "pins_digital_write", (block) => field(block, "PIN") === "8" && (field(block, "STATE") || "HIGH") === state);

export const MELODY_FUNCTION = "melodie";

/** The C++ of "My code" that the tutorial writes: a short tune on the buzzer of pin 6. */
export function melodyCode(language: Language): string {
	const notes = language === "fr" ? ["do", "mi", "sol"] : ["C", "E", "G"];
	return [
		language === "fr" ? "// Une petite mélodie sur le buzzer de la broche 6." : "// A short tune on the buzzer of pin 6.",
		`void ${MELODY_FUNCTION}() {`,
		`  tone(6, 523, 150); // ${notes[0]}`,
		"  delay(200);",
		`  tone(6, 659, 150); // ${notes[1]}`,
		"  delay(200);",
		`  tone(6, 784, 300); // ${notes[2]}`,
		"  delay(350);",
		"}",
		"",
	].join("\n");
}

const hasMelody = (userCode: UserCode | null) => (userCode?.code ?? "").includes(`${MELODY_FUNCTION}(`);

// --- the steps ----------------------------------------------------------------------------

export const PROJECT_NAME: Localized = { fr: "Ma sonnette", en: "My doorbell" };

export const steps: TourStep[] = [
	{
		id: "welcome",
		title: { fr: "Fabriquons une sonnette !", en: "Let's make a doorbell!" },
		text: {
			fr: "Quand on appuie sur un bouton, une LED s'allume et un buzzer joue une petite mélodie. Tu vas construire le programme avec des blocs, voir le montage, l'essayer sur une carte virtuelle, puis ajouter un peu de vrai code. Ce cadre te guide : le bouton Suivant s'active quand l'étape est faite.",
			en: "When a button is pressed, an LED lights up and a buzzer plays a short tune. You will build the program with blocks, see the wiring, try it on a virtual board, then add a little real code. This box guides you: the Next button turns on when the step is done.",
		},
		done: () => true,
	},
	{
		id: "start",
		title: { fr: "Le bloc de départ", en: "The start block" },
		text: {
			fr: "Tout programme commence par \"quand le programme démarre\", dans la famille Événements. Il est déjà dans ton projet : s'il n'y est plus, glisse-le dans la zone de travail.",
			en: "Every program starts with \"when the program starts\", in the Events category. It is already in your project: if it is not there any more, drag it to the work area.",
		},
		target: { kind: "category", id: "events" },
		done: (context) => mainScript(context.project).length > 0,
	},
	{
		id: "forever",
		title: { fr: "Recommencer sans fin", en: "Again and again" },
		text: {
			fr: "Une sonnette doit toujours surveiller son bouton. Dans la famille Contrôle, glisse \"répéter indéfiniment\" et accroche-le sous \"quand le programme démarre\".",
			en: "A doorbell must always watch its button. From the Control category, drag \"forever\" and attach it under \"when the program starts\".",
		},
		target: { kind: "category", id: "control" },
		done: (context) => forever(context.project) !== undefined,
	},
	{
		id: "if",
		title: { fr: "Une décision", en: "A decision" },
		text: {
			fr: "Toujours dans Contrôle, prends \"si ... alors ... sinon\" et mets-le dans la bouche de \"répéter indéfiniment\".",
			en: "Still in Control, take \"if ... then ... else\" and put it inside the mouth of \"forever\".",
		},
		target: { kind: "category", id: "control" },
		done: (context) => ifElse(context.project) !== undefined,
	},
	{
		id: "button",
		title: { fr: "La question : le bouton est-il appuyé ?", en: "The question: is the button pressed?" },
		text: {
			fr: "Dans la famille Capteurs, glisse \"le bouton de la broche 2 est appuyé\" dans le trou en forme d'hexagone du \"si\".",
			en: "From the Sensors category, drag \"button of pin 2 is pressed\" into the hexagon-shaped hole of \"if\".",
		},
		target: { kind: "category", id: "sensors" },
		done: (context) => ifElse(context.project)?.inputs?.CONDITION?.block?.type === "sensors_button_pressed",
	},
	{
		id: "led",
		title: { fr: "Allumer et éteindre la LED", en: "Turn the LED on and off" },
		text: {
			fr: "Dans Broches, prends \"mettre la broche ... à l'état HAUT\" : mets-le dans \"alors\" et choisis la broche 8. Mets-en un deuxième dans \"sinon\", broche 8, état BAS.",
			en: "From Pins, take \"set pin ... to HIGH\": put it in \"then\" and choose pin 8. Put a second one in \"else\", pin 8, LOW.",
		},
		target: { kind: "category", id: "pins" },
		done: (context) => {
			const decision = ifElse(context.project);
			return ledBlock(decision?.statements?.SUBSTACK, "HIGH") && ledBlock(decision?.statements?.SUBSTACK2, "LOW");
		},
	},
	{
		id: "wiring",
		title: { fr: "Le montage", en: "The wiring" },
		text: {
			fr: "Scratch2Arduino dessine le montage à partir de tes blocs. Clique sur le bouton Câblage (la prise) : tu vois où brancher le bouton et la LED avec sa résistance. Tu pourras le refaire avec de vrais composants.",
			en: "Scratch2Arduino draws the wiring from your blocks. Click the Wiring button (the plug): you see where to plug the button and the LED with its resistor. You can build it with real parts later.",
		},
		target: { kind: "label", key: "toolbar.wiring" },
		done: (context) => context.wiringShown,
	},
	{
		id: "live",
		title: { fr: "Essayer sans carte", en: "Try it without a board" },
		text: {
			fr: "Ferme le schéma. Dans la liste des ports, choisis \"Carte virtuelle (sans matériel)\", puis clique sur Live (le triangle). Le panneau de la carte virtuelle s'ouvre.",
			en: "Close the diagram. In the list of ports, choose \"Virtual board (no hardware)\", then click Live (the triangle). The virtual board panel opens.",
		},
		target: { kind: "label", key: "toolbar.live" },
		done: (context) => context.live && context.simulated,
	},
	{
		id: "press",
		title: { fr: "Sonne !", en: "Ring!" },
		text: {
			fr: "Dans le panneau de la carte virtuelle, garde le bouton \"Appuie\" enfoncé : la LED de la broche 8 s'allume. Lâche-le : elle s'éteint.",
			en: "In the virtual board panel, hold the \"Press\" button down: the LED of pin 8 lights up. Let go: it goes off.",
		},
		target: { kind: "text", text: { fr: "Appuie", en: "Press" } },
		done: (context, memory) => {
			if (context.live && context.pressed.includes("2")) memory.pressed = true;
			return memory.pressed === true;
		},
	},
	{
		id: "buzzer",
		title: { fr: "Un son", en: "A sound" },
		text: {
			fr: "Dans la famille Son, glisse \"jouer 440 Hz pendant 0.5 secondes sur le buzzer de la broche ...\" dans \"alors\", sous la LED, et choisis la broche 6. Appuie de nouveau sur le bouton virtuel : tu entends le buzzer (le haut-parleur de l'ordinateur).",
			en: "From the Sound category, drag \"play 440 Hz for 0.5 seconds on the buzzer of pin ...\" into \"then\", below the LED, and choose pin 6. Press the virtual button again: you hear the buzzer (the computer's speaker).",
		},
		target: { kind: "category", id: "sound" },
		done: (context) => has(ifElse(context.project)?.statements?.SUBSTACK, "sound_play_tone", (block) => field(block, "PIN") === "6"),
	},
	{
		id: "code",
		title: { fr: "Un peu de vrai code", en: "A little real code" },
		text: {
			fr: "Les blocs deviennent du code C++, comme celui des développeurs. Arrête Live, passe sur la vue Code, onglet \"Mon code\", puis clique sur le bouton ci-dessous : il écrit pour toi une fonction melodie() qui joue trois notes.",
			en: "Blocks become C++ code, like the code of developers. Stop Live, go to the Code view, \"My code\" tab, then click the button below: it writes a melodie() function that plays three notes for you.",
		},
		target: { kind: "label", key: "view.label" },
		action: { label: { fr: "Écrire la fonction melodie()", en: "Write the melodie() function" }, run: "insert-melody" },
		done: (context) => hasMelody(context.userCode),
	},
	{
		id: "melody-block",
		title: { fr: "Ta fonction devient un bloc", en: "Your function becomes a block" },
		text: {
			fr: "Reviens à la vue Blocs. Dans la famille Mon code, un bloc \"melodie\" est apparu : mets-le dans \"alors\" à la place du bloc du buzzer (jette l'ancien bloc dans la poubelle).",
			en: "Go back to the Blocks view. In the My code category, a \"melodie\" block appeared: put it in \"then\" instead of the buzzer block (throw the old block in the bin).",
		},
		target: { kind: "category", id: "mycode" },
		done: (context) =>
			has(ifElse(context.project)?.statements?.SUBSTACK, "mycode_call", (block) => field(block, "NAME") === MELODY_FUNCTION),
	},
	{
		id: "check",
		title: { fr: "Vérifier", en: "Check" },
		text: {
			fr: "Clique sur Vérifier (la coche verte) : Scratch2Arduino compile tes blocs et ton code C++ ensemble, comme pour une vraie carte. S'il y a une erreur, il te montre le bloc ou la ligne à corriger.",
			en: "Click Check (the green tick): Scratch2Arduino compiles your blocks and your C++ code together, as for a real board. If there is a mistake, it shows you the block or the line to fix.",
		},
		target: { kind: "label", key: "toolbar.build" },
		done: (context, _memory, start) => context.checks > start.checks,
	},
	{
		id: "python",
		title: { fr: "Le même code en Python", en: "The same code in Python" },
		text: {
			fr: "Le code C++ tourne sur la carte, pas en Live. Dans la vue Code, onglet \"Mon code\", choisis Python dans \"Langage de mon code\" et accepte : ta fonction est traduite en Python, qui tourne sur l'ordinateur avec Live. Compare les deux versions !",
			en: "C++ code runs on the board, not in Live mode. In the Code view, \"My code\" tab, choose Python in \"Language of my code\" and accept: your function is translated into Python, which runs on the computer with Live. Compare both versions!",
		},
		target: { kind: "label", key: "code.mine_language" },
		done: (context) => context.userCode?.language === "python" && hasMelody(context.userCode),
	},
	{
		id: "live-melody",
		title: { fr: "La mélodie en Live", en: "The tune in Live mode" },
		text: {
			fr: "Reviens aux blocs, relance Live sur la carte virtuelle et appuie sur le bouton : la LED s'allume et la mélodie joue.",
			en: "Go back to the blocks, start Live again on the virtual board and press the button: the LED lights up and the tune plays.",
		},
		target: { kind: "label", key: "toolbar.live" },
		done: (context, memory) => {
			if (context.live && context.userCode?.language === "python" && context.pressed.includes("2")) memory.pressed = true;
			return memory.pressed === true;
		},
	},
	{
		id: "done",
		title: { fr: "Bravo, ta sonnette marche !", en: "Well done, your doorbell works!" },
		text: {
			fr: "Pour la vraie carte : fais le montage du schéma Câblage (bouton sur 2, LED et résistance sur 8, buzzer sur 6), branche la carte et clique sur Téléverser. Le code Python de \"Mon code\" est converti en C++ tout seul. Enregistre ton projet pour le garder.",
			en: "For the real board: build the wiring of the Wiring diagram (button on 2, LED and resistor on 8, buzzer on 6), plug the board in and click Upload. The Python of \"My code\" is converted to C++ by itself. Save your project to keep it.",
		},
		done: () => true,
	},
];
