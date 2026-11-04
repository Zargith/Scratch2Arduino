import {
	ActionIcon,
	Alert,
	AppShell,
	Button,
	Code,
	Group,
	Loader,
	Modal,
	ScrollArea,
	SegmentedControl,
	Select,
	Spoiler,
	Text,
	TextInput,
	Title,
	Tooltip,
} from "@mantine/core";
import {
	IconCheck,
	IconCode,
	IconDeviceFloppy,
	IconFilePlus,
	IconFolderOpen,
	IconRefresh,
	IconUpload,
} from "@tabler/icons-react";
import { useCallback, useEffect, useRef, useState } from "react";

import { bridge, hasHost, HostMissingError, type Outcome, type Port } from "@/bridge";
import { boards, defaultBoardId, findBoard, type Language } from "@/catalog";
import { Editor, type EditorHandle } from "@/Editor";
import { detectLanguage, translate, type MessageKey } from "@/i18n";
import {
	emptyProject,
	projectToWorkspace,
	workspaceToProject,
	type Project,
	type WorkspaceState,
} from "@/project/convert";

interface Status {
	kind: "info" | "success" | "error";
	message: string;
	details?: string;
}

const starterProject = (name: string): Project => ({
	...emptyProject(name, defaultBoardId),
	scripts: [{ x: 40, y: 40, blocks: [{ id: "start", type: "events_when_program_starts" }] }],
});

export function App() {
	const [language, setLanguage] = useState<Language>(detectLanguage);
	const t = useCallback(
		(key: MessageKey, params?: Record<string, string>) => translate(language, key, params),
		[language]
	);

	const editor = useRef<EditorHandle>(null);
	const [editorKey, setEditorKey] = useState(0);
	const initialState = useRef<WorkspaceState>(projectToWorkspace(starterProject("")));
	const initialValues = useRef<Record<string, number>>({});

	const [name, setName] = useState(() => translate(detectLanguage(), "project.untitled"));
	const [boardId, setBoardId] = useState(defaultBoardId);
	const [path, setPath] = useState("");
	const [dirty, setDirty] = useState(false);
	const [ports, setPorts] = useState<Port[]>([]);
	const [port, setPort] = useState<string | null>(null);
	const [busy, setBusy] = useState(false);
	const [status, setStatus] = useState<Status | null>(null);
	const [code, setCode] = useState<string | null>(null);

	const currentProject = useCallback(
		(): Project =>
			workspaceToProject(editor.current?.save() ?? initialState.current, {
				name,
				board: boardId,
				initialValues: initialValues.current,
			}),
		[name, boardId]
	);

	const fail = useCallback(
		(error: unknown) => {
			if (error instanceof HostMissingError) setStatus({ kind: "info", message: t("status.no_host") });
			// The host rejects with a message already written for the user.
			else if (typeof error === "string") setStatus({ kind: "error", message: error });
			else setStatus({ kind: "error", message: t("status.error", { message: String(error) }) });
		},
		[t]
	);

	/** Recreate the editor from a project (open, new) or from the current blocks (language). */
	const reload = useCallback((state: WorkspaceState) => {
		initialState.current = state;
		setEditorKey((key) => key + 1);
	}, []);

	const refreshPorts = useCallback(async () => {
		if (!hasHost()) return;
		try {
			const found = await bridge.ports();
			setPorts(found);
			setPort((current) =>
				current && found.some((candidate) => candidate.address === current) ? current : (found[0]?.address ?? null)
			);
			const detected = found[0]?.boardId;
			if (detected && findBoard(detected)) setBoardId(detected);
		} catch (error) {
			fail(error);
		}
	}, [fail]);

	useEffect(() => {
		if (!hasHost()) return;
		void bridge
			.hello()
			.then((info) => setLanguage(info.language))
			.catch(fail);
		void refreshPorts();
	}, [fail, refreshPorts]);

	useEffect(() => {
		if (!hasHost()) return;
		const file = path === "" ? name : path.split(/[\\/]/).pop() ?? name;
		void bridge.setTitle(`${dirty ? "• " : ""}${file} — ${t("app.title")}`).catch(() => undefined);
	}, [name, path, dirty, t]);

	const switchLanguage = (next: Language) => {
		if (next === language) return;
		initialState.current = editor.current?.save() ?? initialState.current;
		setLanguage(next);
		setEditorKey((key) => key + 1);
	};

	const newProject = () => {
		if (dirty && !window.confirm(t("confirm.new"))) return;
		const untitled = t("project.untitled");
		initialValues.current = {};
		setName(untitled);
		setPath("");
		setDirty(false);
		setStatus(null);
		reload(projectToWorkspace(starterProject(untitled)));
	};

	const openProject = async () => {
		try {
			const opened = await bridge.open(language);
			if (!opened) return;
			initialValues.current = Object.fromEntries(
				opened.project.variables.map((variable) => [variable.id, variable.initialValue])
			);
			setName(opened.project.name);
			setBoardId(findBoard(opened.project.board) ? opened.project.board : defaultBoardId);
			setPath(opened.path);
			setDirty(false);
			reload(projectToWorkspace(opened.project));
			setStatus({ kind: "success", message: t("status.opened", { path: opened.path }) });
		} catch (error) {
			fail(error);
		}
	};

	const saveProject = async () => {
		try {
			const saved = await bridge.save(currentProject(), path, language);
			if (!saved) return;
			setPath(saved);
			setDirty(false);
			setStatus({ kind: "success", message: t("status.saved", { path: saved }) });
		} catch (error) {
			fail(error);
		}
	};

	const showCode = async () => {
		try {
			setCode(await bridge.generate(currentProject()));
		} catch (error) {
			fail(error);
		}
	};

	const run = async (kind: "build" | "upload") => {
		if (kind === "upload" && !port) {
			setStatus({ kind: "info", message: t("status.no_port") });
			return;
		}
		setBusy(true);
		setStatus({ kind: "info", message: t(kind === "build" ? "status.building" : "status.uploading") });
		try {
			const project = currentProject();
			const outcome: Outcome =
				kind === "build" ? await bridge.build(project, language) : await bridge.upload(project, port ?? "", language);
			setStatus({ kind: outcome.ok ? "success" : "error", message: outcome.message, details: outcome.details });
		} catch (error) {
			fail(error);
		} finally {
			setBusy(false);
		}
	};

	return (
		<AppShell header={{ height: 64 }} footer={{ height: status ? "auto" : 0 }} padding={0}>
			<AppShell.Header px="md">
				<Group h="100%" gap="sm" wrap="nowrap">
					<Title order={3} style={{ fontFamily: "Space Mono, monospace", whiteSpace: "nowrap" }}>
						{t("app.title")}
					</Title>
					<TextInput
						aria-label={t("project.name")}
						value={name}
						onChange={(event) => {
							setName(event.currentTarget.value);
							setDirty(true);
						}}
						w={180}
					/>
					<Button.Group>
						<Button variant="default" leftSection={<IconFilePlus size={18} />} onClick={newProject}>
							{t("toolbar.new")}
						</Button>
						<Button variant="default" leftSection={<IconFolderOpen size={18} />} onClick={() => void openProject()}>
							{t("toolbar.open")}
						</Button>
						<Button variant="default" leftSection={<IconDeviceFloppy size={18} />} onClick={() => void saveProject()}>
							{t("toolbar.save")}
						</Button>
					</Button.Group>
					<Select
						aria-label={t("board.label")}
						data={boards.map((board) => ({ value: board.id, label: board.name }))}
						value={boardId}
						onChange={(value) => {
							if (value) {
								setBoardId(value);
								setDirty(true);
							}
						}}
						allowDeselect={false}
						w={170}
					/>
					<Select
						aria-label={t("port.label")}
						placeholder={t("port.none")}
						data={ports.map((candidate) => ({ value: candidate.address, label: candidate.label }))}
						value={port}
						onChange={setPort}
						w={190}
					/>
					<Tooltip label={t("port.refresh")}>
						<ActionIcon variant="default" size="lg" aria-label={t("port.refresh")} onClick={() => void refreshPorts()}>
							<IconRefresh size={18} />
						</ActionIcon>
					</Tooltip>
					<Group gap="xs" ml="auto" wrap="nowrap">
						<Button variant="default" leftSection={<IconCode size={18} />} onClick={() => void showCode()}>
							{t("toolbar.code")}
						</Button>
						<Button color="green" leftSection={<IconCheck size={18} />} loading={busy} onClick={() => void run("build")}>
							{t("toolbar.build")}
						</Button>
						<Button color="orange" leftSection={<IconUpload size={18} />} loading={busy} onClick={() => void run("upload")}>
							{t("toolbar.upload")}
						</Button>
						<SegmentedControl
							aria-label={t("language.label")}
							data={[
								{ value: "fr", label: "FR" },
								{ value: "en", label: "EN" },
							]}
							value={language}
							onChange={(value) => switchLanguage(value as Language)}
						/>
					</Group>
				</Group>
			</AppShell.Header>

			<AppShell.Main style={{ position: "relative", height: "100dvh" }}>
				<div style={{ position: "absolute", inset: "64px 0 0 0" }}>
					<Editor
						key={editorKey}
						ref={editor}
						language={language}
						board={findBoard(boardId)}
						initialState={initialState.current}
						onChange={() => setDirty(true)}
					/>
				</div>
			</AppShell.Main>

			{status && (
				<AppShell.Footer p="xs">
					<Alert
						color={status.kind === "error" ? "orange" : status.kind === "success" ? "green" : "blue"}
						withCloseButton
						onClose={() => setStatus(null)}
						icon={busy ? <Loader size="xs" /> : undefined}
					>
						<Text size="sm">{status.message}</Text>
						{status.details && (
							<Spoiler maxHeight={0} showLabel={t("status.details")} hideLabel={t("status.details")}>
								<ScrollArea.Autosize mah={160}>
									<Code block>{status.details}</Code>
								</ScrollArea.Autosize>
							</Spoiler>
						)}
					</Alert>
				</AppShell.Footer>
			)}

			<Modal opened={code !== null} onClose={() => setCode(null)} title={t("code.title")} size="xl">
				<Text size="sm" c="dimmed" mb="sm">
					{t("code.readonly")}
				</Text>
				<ScrollArea.Autosize mah="70vh">
					<Code block>{code}</Code>
				</ScrollArea.Autosize>
			</Modal>
		</AppShell>
	);
}
