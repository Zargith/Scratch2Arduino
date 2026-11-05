import { Button, Group, Modal, Stack, Text, TextInput } from "@mantine/core";
import { forwardRef, useEffect, useImperativeHandle, useRef, useState } from "react";
import * as Blockly from "scratch-blocks";

import {
	blocksTheme,
	CREATE_VARIABLE_CALLBACK,
	defineBlocks,
	setCurrentBoard,
	toolbox,
} from "@/blocks/definitions";
import type { Board, Language } from "@/catalog";
import { translate } from "@/i18n";
import type { WorkspaceState } from "@/project/convert";

type WorkspaceLoadState = Parameters<typeof Blockly.serialization.workspaces.load>[0];

export interface EditorHandle {
	save(): WorkspaceState;
	/** Highlights blocks reported by Check/Upload and scrolls to the first; cleared by the next edit. */
	highlight(blockIds: string[]): void;
	/** Live mode: the scripts holding these blocks glow, as in Scratch. */
	showRunning(blockIds: string[]): void;
}

interface EditorProps {
	language: Language;
	board: Board | undefined;
	/** State restored when the editor is created (language switch, project opened). */
	initialState: WorkspaceState;
	onChange(): void;
	/** Some scripts could not be shown (the others are). */
	onLoadError?(lostScripts: number): void;
}

interface PromptRequest {
	message: string;
	value: string;
	resolve(value: string | null): void;
}

const LOOSE = "S2A_LOOSE_BLOCK";

/**
 * Blocks not attached under a start block do not run: they are shown greyed, like comments,
 * and come back to life as soon as they are attached again.
 */
function markLooseBlocks(workspace: Blockly.WorkspaceSvg) {
	if (workspace.isDragging()) return;
	const recordUndo = Blockly.Events.getRecordUndo();
	Blockly.Events.setRecordUndo(false);
	try {
		for (const block of workspace.getAllBlocks(false)) {
			const root = block.getRootBlock();
			const loose = !!(root.outputConnection || root.previousConnection);
			if (block.hasDisabledReason(LOOSE) !== loose) block.setDisabledReason(loose, LOOSE);
		}
	} finally {
		Blockly.Events.setRecordUndo(recordUndo);
	}
}

export const Editor = forwardRef<EditorHandle, EditorProps>(function Editor(
	{ language, board, initialState, onChange, onLoadError },
	ref
) {
	const container = useRef<HTMLDivElement>(null);
	const workspace = useRef<Blockly.WorkspaceSvg | null>(null);
	const onChangeRef = useRef(onChange);
	onChangeRef.current = onChange;
	const onLoadErrorRef = useRef(onLoadError);
	onLoadErrorRef.current = onLoadError;
	const [prompt, setPrompt] = useState<PromptRequest | null>(null);
	const glowing = useRef<Set<string>>(new Set());

	useImperativeHandle(ref, () => ({
		save: () => (workspace.current ? (Blockly.serialization.workspaces.save(workspace.current) as WorkspaceState) : {}),
		showRunning: (blockIds) => {
			const target = workspace.current;
			if (!target) return;
			const roots = new Set(
				blockIds.map((id) => target.getBlockById(id)?.getRootBlock().id).filter((id): id is string => !!id)
			);
			for (const id of glowing.current) {
				if (!roots.has(id) && target.getBlockById(id)) Blockly.glowStack(id, false);
			}
			for (const id of roots) Blockly.glowStack(id, true);
			glowing.current = roots;
		},
		highlight: (blockIds) => {
			const target = workspace.current;
			if (!target) return;
			target.highlightBlock(null);
			const shown = blockIds.filter((id) => target.getBlockById(id));
			for (const id of shown) target.highlightBlock(id, true);
			if (shown[0]) target.centerOnBlock(shown[0]);
		},
	}));

	useEffect(() => {
		const element = container.current;
		if (!element) return;
		Blockly.ScratchMsgs.setLocale(language);
		defineBlocks(language);
		const createLabel = translate(language, "variables.create");
		const variablesOf = (state: WorkspaceState) => state.variables ?? [];
		const created = Blockly.inject(element, {
			toolbox: toolbox(language, variablesOf(initialState), createLabel),
			theme: blocksTheme(),
			media: "./media/",
			trashcan: true,
			sounds: false,
			zoom: { controls: true, wheel: true, startScale: 0.675 },
			grid: { spacing: 40, length: 2, colour: "#ddd" },
		});
		created.registerButtonCallback(CREATE_VARIABLE_CALLBACK, () => {
			Blockly.ScratchVariables.createVariable(created);
		});
		Blockly.ScratchVariables.setPromptHandler((message, defaultValue, callback) => {
			setPrompt({
				message: message || translate(language, "variables.prompt"),
				value: defaultValue,
				resolve: (value) => {
					if (value !== null) callback(value, []);
				},
			});
		});
		Blockly.Events.disable();
		try {
			Blockly.serialization.workspaces.load(initialState as WorkspaceLoadState, created);
		} catch (error) {
			// A project the editor cannot show must not take the whole window down: show what can
			// be shown, script by script, and tell how many scripts are missing.
			console.error("cannot load the project into the editor", error);
			created.clear();
			let lost = 0;
			try {
				Blockly.serialization.workspaces.load(
					{ ...initialState, blocks: { languageVersion: 0, blocks: [] } } as WorkspaceLoadState,
					created
				);
			} catch {
				created.clear();
			}
			for (const top of initialState.blocks?.blocks ?? []) {
				try {
					Blockly.serialization.blocks.append(top as Parameters<typeof Blockly.serialization.blocks.append>[0], created);
				} catch {
					lost += 1;
				}
			}
			onLoadErrorRef.current?.(Math.max(lost, 1));
		} finally {
			Blockly.Events.enable();
		}
		Blockly.Events.disable(); // opening a project does not modify it
		try {
			markLooseBlocks(created);
		} finally {
			Blockly.Events.enable();
		}
		const variableEvents: string[] = [Blockly.Events.VAR_CREATE, Blockly.Events.VAR_DELETE, Blockly.Events.VAR_RENAME];
		const blockEvents: string[] = [Blockly.Events.BLOCK_CREATE, Blockly.Events.BLOCK_MOVE, Blockly.Events.BLOCK_DELETE];
		created.addChangeListener((event: Blockly.Events.Abstract) => {
			if (variableEvents.includes(event.type)) {
				const variables = created.getVariableMap().getVariablesOfType("").map((variable) => ({ id: variable.getId() }));
				created.updateToolbox(toolbox(language, variables, createLabel));
				// ScratchContinuousToolbox only redraws its flyout on request.
				(created.getToolbox() as { forceRerender?: () => void } | null)?.forceRerender?.();
			}
			if (blockEvents.includes(event.type)) markLooseBlocks(created);
			if (!event.isUiEvent) {
				created.highlightBlock(null);
				onChangeRef.current();
			}
		});
		workspace.current = created;

		const resize = new ResizeObserver(() => Blockly.svgResize(created));
		resize.observe(element);
		return () => {
			resize.disconnect();
			created.dispose();
			workspace.current = null;
		};
		// The editor is rebuilt (with key=language) when the language changes; initialState
		// is only read once.
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, []);

	useEffect(() => {
		setCurrentBoard(board);
	}, [board]);

	const close = (value: string | null) => {
		prompt?.resolve(value);
		setPrompt(null);
	};

	return (
		<>
			<div ref={container} style={{ position: "absolute", inset: 0 }} />
			<Modal opened={prompt !== null} onClose={() => close(null)} withCloseButton={false} centered>
				<form
					onSubmit={(event) => {
						event.preventDefault();
						close(prompt?.value ?? null);
					}}
				>
					<Stack>
						<Text>{prompt?.message}</Text>
						<TextInput
							data-autofocus
							value={prompt?.value ?? ""}
							onChange={(event) => {
								const value = event.currentTarget.value;
								setPrompt((current) => (current ? { ...current, value } : current));
							}}
						/>
						<Group justify="flex-end">
							<Button variant="default" onClick={() => close(null)}>
								{translate(language, "dialog.cancel")}
							</Button>
							<Button type="submit">{translate(language, "dialog.ok")}</Button>
						</Group>
					</Stack>
				</form>
			</Modal>
		</>
	);
});
