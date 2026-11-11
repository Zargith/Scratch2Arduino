// Code editor of the C++, Python and JavaScript modes (CodeMirror 6, bundled offline).
// Mistakes are shown only when the host reports them (Check, Upload, Live), never while typing:
// no linter runs on the text, the problems are set from outside and follow the edits.
import { autocompletion, closeBrackets, closeBracketsKeymap, completionKeymap } from "@codemirror/autocomplete";
import { defaultKeymap, history, historyKeymap, indentWithTab } from "@codemirror/commands";
import { cpp } from "@codemirror/lang-cpp";
import { javascript } from "@codemirror/lang-javascript";
import { python } from "@codemirror/lang-python";
import { lintGutter, setDiagnostics, type Diagnostic } from "@codemirror/lint";
import {
	bracketMatching,
	foldGutter,
	foldKeymap,
	HighlightStyle,
	indentOnInput,
	syntaxHighlighting,
} from "@codemirror/language";
import { highlightSelectionMatches, searchKeymap } from "@codemirror/search";
import { Compartment, EditorState, type Extension } from "@codemirror/state";
import {
	drawSelection,
	dropCursor,
	EditorView,
	highlightActiveLine,
	highlightActiveLineGutter,
	highlightSpecialChars,
	keymap,
	lineNumbers,
} from "@codemirror/view";
import { tags } from "@lezer/highlight";
import { forwardRef, useEffect, useImperativeHandle, useRef } from "react";

import type { Language } from "@/catalog";
import { completionSource, type TextLanguage } from "@/codeHelp";
import { problemRange, type CodeProblem } from "@/program";

export interface CodeEditorHandle {
	/** Marks the faulty lines (underline, gutter, message on hover) and scrolls to the first one. */
	showProblems(problems: CodeProblem[]): void;
	/** Removes the marks of the last Check. */
	clearProblems(): void;
}

interface CodeEditorProps {
	program: TextLanguage;
	language: Language;
	/** Code shown when the editor is created; later changes come from the user (or `readOnly` updates). */
	initialCode: string;
	onChange?(code: string): void;
	/** Shown, not edited: the code made from the blocks. Its text follows `initialCode`. */
	readOnly?: boolean;
	/** Mistakes to show when the editor opens (the last Check, Upload or Live). */
	problems?: CodeProblem[];
}

/** The editor of the code editor: like CodeMirror's basicSetup, with the light completion of codeHelp.ts. */
function setup(program: TextLanguage, language: () => Language, readOnly: boolean): Extension {
	return [
		lineNumbers(),
		highlightActiveLineGutter(),
		highlightSpecialChars(),
		history(),
		foldGutter(),
		drawSelection(),
		dropCursor(),
		EditorState.allowMultipleSelections.of(true),
		indentOnInput(),
		syntaxHighlighting(colours),
		bracketMatching(),
		closeBrackets(),
		autocompletion({ override: [completionSource(program, language)], icons: true }),
		highlightActiveLine(),
		highlightSelectionMatches(),
		keymap.of([
			...closeBracketsKeymap,
			...defaultKeymap,
			...searchKeymap,
			...historyKeymap,
			...foldKeymap,
			...completionKeymap,
			indentWithTab,
		]),
		EditorState.readOnly.of(readOnly),
		EditorView.editable.of(!readOnly),
	];
}

function syntax(program: TextLanguage): Extension {
	if (program === "python") return python();
	if (program === "javascript") return javascript();
	return cpp();
}

/** French texts of CodeMirror's own panels (search, replace, folding...). */
const frenchPhrases: Record<string, string> = {
	Find: "Chercher",
	Replace: "Remplacer",
	next: "suivant",
	previous: "précédent",
	all: "tout",
	"match case": "respecter la casse",
	"by word": "mot entier",
	regexp: "expression régulière",
	replace: "remplacer",
	"replace all": "tout remplacer",
	close: "fermer",
	"Go to line": "Aller à la ligne",
	go: "aller",
	"Folded lines": "Lignes repliées",
	"Unfolded lines": "Lignes dépliées",
	"Fold line": "Replier la ligne",
	"Unfold line": "Déplier la ligne",
	Diagnostics: "Erreurs",
	"No diagnostics": "Aucune erreur",
};

/** Code colours from the Mantine palette: readable in the light and the dark themes. */
const colours = HighlightStyle.define([
	{ tag: [tags.keyword, tags.controlKeyword, tags.definitionKeyword, tags.moduleKeyword], color: "var(--mantine-color-violet-text)" },
	{ tag: [tags.number, tags.bool, tags.null, tags.atom], color: "var(--mantine-color-teal-text)" },
	{ tag: [tags.string, tags.special(tags.string)], color: "var(--mantine-color-orange-text)" },
	{ tag: [tags.comment, tags.lineComment, tags.blockComment], color: "var(--mantine-color-dimmed)", fontStyle: "italic" },
	{ tag: [tags.function(tags.variableName), tags.function(tags.propertyName)], color: "var(--mantine-color-blue-text)" },
	{ tag: [tags.typeName, tags.className, tags.standard(tags.typeName)], color: "var(--mantine-color-cyan-text)" },
	{ tag: [tags.processingInstruction, tags.macroName], color: "var(--mantine-color-pink-text)" },
	{ tag: tags.invalid, color: "var(--mantine-color-red-text)" },
]);

const look = EditorView.theme({
	"&": {
		height: "100%",
		fontSize: "15px",
		backgroundColor: "var(--mantine-color-body)",
		color: "var(--mantine-color-text)",
	},
	".cm-gutters": {
		backgroundColor: "var(--mantine-color-default-hover)",
		color: "var(--mantine-color-dimmed)",
		borderRight: "1px solid var(--mantine-color-default-border)",
	},
	".cm-activeLine, .cm-activeLineGutter": { backgroundColor: "var(--mantine-color-default-hover)" },
	".cm-cursor": { borderLeftColor: "var(--mantine-color-text)" },
	"&.cm-focused .cm-selectionBackground, .cm-selectionBackground": {
		backgroundColor: "var(--mantine-primary-color-light) !important",
	},
	".cm-tooltip": {
		backgroundColor: "var(--mantine-color-body)",
		color: "var(--mantine-color-text)",
		border: "1px solid var(--mantine-color-default-border)",
	},
	".cm-scroller": { fontFamily: "'Space Mono', monospace", lineHeight: "1.6" },
	".cm-content": { paddingBottom: "40vh" }, // the last lines are not hidden by the status bar
});

export const CodeEditor = forwardRef<CodeEditorHandle, CodeEditorProps>(function CodeEditor(
	{ program, language, initialCode, onChange, readOnly = false, problems },
	ref
) {
	const host = useRef<HTMLDivElement>(null);
	const view = useRef<EditorView | null>(null);
	const syntaxSlot = useRef(new Compartment());
	const phrasesSlot = useRef(new Compartment());
	const onChangeRef = useRef(onChange);
	onChangeRef.current = onChange;
	const languageRef = useRef(language);
	languageRef.current = language;

	// Created once; the language and the texts are switched in place.
	useEffect(() => {
		if (!host.current) return;
		const editor = new EditorView({
			parent: host.current,
			state: EditorState.create({
				doc: initialCode,
				extensions: [
					setup(program, () => languageRef.current, readOnly),
					lintGutter(),
					look,
					EditorView.lineWrapping,
					syntaxSlot.current.of(syntax(program)),
					phrasesSlot.current.of(EditorState.phrases.of(language === "fr" ? frenchPhrases : {})),
					EditorView.updateListener.of((update) => {
						if (update.docChanged && !readOnly) onChangeRef.current?.(update.state.doc.toString());
					}),
				],
			}),
		});
		view.current = editor;
		return () => {
			editor.destroy();
			view.current = null;
		};
		// The initial code and language are read once: the editor is recreated with a new key.
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, []);

	useEffect(() => {
		view.current?.dispatch({ effects: syntaxSlot.current.reconfigure(syntax(program)) });
	}, [program]);

	// A read-only view follows its text (the code of the blocks, regenerated).
	useEffect(() => {
		const editor = view.current;
		if (!readOnly || !editor || editor.state.doc.toString() === initialCode) return;
		editor.dispatch({ changes: { from: 0, to: editor.state.doc.length, insert: initialCode } });
	}, [readOnly, initialCode]);

	useEffect(() => {
		view.current?.dispatch({
			effects: phrasesSlot.current.reconfigure(EditorState.phrases.of(language === "fr" ? frenchPhrases : {})),
		});
	}, [language]);

	const show = (problems: CodeProblem[], focus: boolean) => {
		const editor = view.current;
		if (!editor) return;
		const code = editor.state.doc.toString();
		const diagnostics: Diagnostic[] = problems.map((problem) => ({
			...problemRange(code, problem),
			severity: "error",
			message: problem.message,
		}));
		editor.dispatch(setDiagnostics(editor.state, diagnostics));
		if (diagnostics.length > 0 && focus) {
			editor.dispatch({
				selection: { anchor: diagnostics[0].from },
				effects: EditorView.scrollIntoView(diagnostics[0].from, { y: "center" }),
			});
			editor.focus();
		}
	};

	useEffect(() => {
		// An empty list clears the marks of an earlier Check (the code was fixed).
		show(problems ?? [], false);
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, [problems]);

	useImperativeHandle(ref, () => ({
		showProblems: (problems) => show(problems, true),
		clearProblems: () => {
			const editor = view.current;
			if (editor) editor.dispatch(setDiagnostics(editor.state, []));
		},
	}));

	return <div ref={host} style={{ height: "100%", overflow: "hidden" }} data-testid="code-editor" />;
});
