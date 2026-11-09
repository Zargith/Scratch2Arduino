import {
	ActionIcon,
	Alert,
	AppShell,
	Button,
	Card,
	Code,
	Group,
	Loader,
	Menu,
	Modal,
	ScrollArea,
	SegmentedControl,
	Select,
	Spoiler,
	Stack,
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
	IconHistory,
	IconPlayerPlay,
	IconPlayerStop,
	IconPlugConnected,
	IconRefresh,
	IconTerminal2,
	IconUpload,
} from "@tabler/icons-react";
import { useCallback, useEffect, useRef, useState } from "react";

import {
	bridge,
	hasHost,
	HostMissingError,
	type AutosavedProject,
	type LiveSnapshot,
	type OpenedProject,
	type Outcome,
	type Port,
	type RecentProject,
} from "@/bridge";
import { boards, defaultBoardId, findBoard, type Language } from "@/catalog";
import { Editor, type EditorHandle } from "@/Editor";
import { exampleProject, type Example } from "@/examples";
import { Gallery, Wiring } from "@/Gallery";
import logo from "@/logo.png";
import { SerialMonitor } from "@/SerialMonitor";
import { Sparkline } from "@/Sparkline";
import { detectLanguage, translate, type MessageKey } from "@/i18n";
import {
	emptyProject,
	projectToWorkspace,
	unknownBlockTypes,
	workspaceToProject,
	type Project,
	type WorkspaceState,
} from "@/project/convert";

/** How often a modified project is copied aside, in case the program is closed without saving. */
const autosaveInterval = 15_000;

/** Points kept per graph: 30 s at one snapshot every 200 ms. */
const historyLength = 150;

interface Status {
	kind: "info" | "success" | "error";
	message: string;
	details?: string;
}

const starterProject = (name: string): Project => ({
	...emptyProject(name, defaultBoardId),
	scripts: [{ x: 40, y: 40, blocks: [{ id: "start", type: "events_when_program_starts" }] }],
});

/** "Ctrl+S", or "⌘S" on a Mac. */
function shortcut(key: string): string {
	return /Mac|iPhone|iPad/.test(navigator.platform) ? `⌘${key}` : `Ctrl+${key}`;
}

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
	const [live, setLive] = useState(false);
	const [liveSnapshot, setLiveSnapshot] = useState<LiveSnapshot | null>(null);
	const [variableNames, setVariableNames] = useState<Map<string, string>>(new Map());
	const liveUpdate = useRef<ReturnType<typeof setTimeout> | null>(null);
	/** Last values of each variable ("var:<id>") and sensor ("pin:<label>"), for the graphs. */
	const history = useRef<Map<string, number[]>>(new Map());
	const [monitorShown, setMonitorShown] = useState(false);
	const [galleryShown, setGalleryShown] = useState(false);
	/** The example being edited: its wiring diagram stays one click away. */
	const [example, setExample] = useState<Example | null>(null);
	const [wiringShown, setWiringShown] = useState(false);
	const [recent, setRecent] = useState<RecentProject[]>([]);
	/** Unsaved work found at start-up, offered back to the user. */
	const [restore, setRestore] = useState<AutosavedProject | null>(null);
	/** Edits made so far, and how many the last autosave holds. */
	const edits = useRef(0);
	const autosavedEdits = useRef(0);
	/** Autosave calls run one after the other (a save never races with an older autosave). */
	const autosaving = useRef<Promise<unknown>>(Promise.resolve());

	const markDirty = useCallback(() => {
		edits.current += 1;
		setDirty(true);
	}, []);

	/** The project on screen is saved or replaced: its autosaved copy is no longer needed. */
	const forgetAutosave = useCallback(() => {
		autosavedEdits.current = edits.current;
		if (!hasHost()) return;
		autosaving.current = autosaving.current.then(() => bridge.autosaveClear()).catch(() => undefined);
	}, []);

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
		const file = path === "" ? name : path.split(/[\\/]/).pop() ?? name;
		void bridge.setTitle(`${dirty ? "• " : ""}${file} — ${t("app.title")}`).catch(() => undefined);
	}, [name, path, dirty, t]);

	const switchLanguage = (next: Language) => {
		if (next === language) return;
		initialState.current = editor.current?.save() ?? initialState.current;
		// A project still named "My project" follows the language.
		setName((current) => (current === translate(language, "project.untitled") ? translate(next, "project.untitled") : current));
		setLanguage(next);
		setEditorKey((key) => key + 1);
	};
	const switchLanguageRef = useRef(switchLanguage);
	switchLanguageRef.current = switchLanguage;

	// Once, at start-up: the host tells the system language. Later language changes are the
	// user's (the FR/EN switch) and must not be overridden.
	const startup = useRef({ fail, refreshPorts });
	useEffect(() => {
		if (!hasHost()) return;
		void bridge
			.hello()
			.then((info) => switchLanguageRef.current(info.language))
			.catch(startup.current.fail);
		void startup.current.refreshPorts();
	}, []);

	const newProject = () => {
		if (dirty && !window.confirm(t("confirm.new"))) return;
		setGalleryShown(false);
		const untitled = t("project.untitled");
		initialValues.current = {};
		setName(untitled);
		setPath("");
		setDirty(false);
		setStatus(null);
		setExample(null);
		forgetAutosave();
		reload(projectToWorkspace(starterProject(untitled)));
	};

	const openExample = (chosen: Example) => {
		if (dirty && !window.confirm(t("confirm.new"))) return;
		setGalleryShown(false);
		const project = exampleProject(chosen, language);
		showOpened({ path: "", project });
		setExample(chosen);
		setStatus({ kind: "success", message: t("status.example_opened", { name: project.name }) });
	};

	const showOpened = (opened: OpenedProject) => {
		initialValues.current = Object.fromEntries(
				opened.project.variables.map((variable) => [variable.id, variable.initialValue])
			);
			setName(opened.project.name);
			setBoardId(findBoard(opened.project.board) ? opened.project.board : defaultBoardId);
			setPath(opened.path);
			setDirty(false);
			setExample(null);
			reload(projectToWorkspace(opened.project));
			const unknown = unknownBlockTypes(opened.project);
			forgetAutosave();
			setStatus(
				unknown.length > 0
					? { kind: "error", message: t("status.unknown_blocks"), details: unknown.join("\n") }
					: { kind: "success", message: t("status.opened", { path: opened.name ?? opened.path }) }
			);
	};

	const openProject = async () => {
		try {
			const opened = await bridge.open(language);
			if (opened) showOpened(opened);
		} catch (error) {
			fail(error);
		}
	};

	// A project dropped on the program's icon (or a double-clicked .s2a file) opens at start-up.
	const showOpenedRef = useRef(showOpened);
	showOpenedRef.current = showOpened;
	useEffect(() => {
		if (!hasHost()) return;
		// The autosave is read first: opening the startup project forgets it.
		const found = bridge.autosaved().catch(() => null);
		void found
			.then(() => bridge.startupProject(language))
			.then((opened) => {
				if (opened) showOpenedRef.current(opened);
			})
			.catch(fail)
			.then(() => found)
			.then((saved) => {
				if (saved) setRestore(saved);
			});
		// Once, when the editor starts.
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, []);

	const saveProject = async () => {
		try {
			const project = currentProject();
			const savedEdits = edits.current;
			await autosaving.current; // an older autosave must not land after the save
			const saved = await bridge.save(project, path, language);
			if (!saved) return;
			setPath(saved);
			autosavedEdits.current = savedEdits; // the host dropped the autosave
			if (edits.current === savedEdits) setDirty(false);
			setStatus({ kind: "success", message: t("status.saved", { path: saved }) });
		} catch (error) {
			fail(error);
		}
	};

	// Ctrl+S (Cmd+S on a Mac) saves, wherever the focus is.
	const saveRef = useRef(saveProject);
	saveRef.current = saveProject;
	useEffect(() => {
		const onKey = (event: KeyboardEvent) => {
			if ((event.ctrlKey || event.metaKey) && !event.altKey && event.key.toLowerCase() === "s") {
				event.preventDefault();
				void saveRef.current();
			}
		};
		document.addEventListener("keydown", onKey);
		return () => document.removeEventListener("keydown", onKey);
	}, []);

	// Every few seconds, a modified project is copied aside by the host.
	const autosaveState = useRef({ currentProject, path });
	autosaveState.current = { currentProject, path };
	useEffect(() => {
		if (!hasHost()) return;
		const timer = setInterval(() => {
			if (edits.current === autosavedEdits.current) return;
			autosavedEdits.current = edits.current;
			const { currentProject: snapshot, path: savedPath } = autosaveState.current;
			const project = snapshot();
			autosaving.current = autosaving.current
				.then(() => bridge.autosave(project, savedPath))
				.catch(() => undefined); // best effort: the user's own save still works
		}, autosaveInterval);
		return () => clearInterval(timer);
	}, []);

	const loadRecent = async () => {
		if (!hasHost()) return;
		try {
			setRecent(await bridge.recent());
		} catch {
			setRecent([]);
		}
	};

	const openRecent = async (recentPath: string) => {
		if (dirty && !window.confirm(t("confirm.new"))) return;
		try {
			showOpened(await bridge.openRecent(recentPath, language));
		} catch (error) {
			fail(error);
		}
	};

	const acceptRestore = () => {
		if (!restore) return;
		const { project } = restore;
		setRestore(null);
		initialValues.current = Object.fromEntries(project.variables.map((variable) => [variable.id, variable.initialValue]));
		setName(project.name);
		setBoardId(findBoard(project.board) ? project.board : defaultBoardId);
		setPath(restore.path);
		setExample(null);
		markDirty(); // still to be saved: the copy stays until then
		reload(projectToWorkspace(project));
		setStatus({ kind: "success", message: t("status.restored", { name: project.name }) });
	};

	const discardRestore = () => {
		setRestore(null);
		if (!dirty) forgetAutosave();
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
			const warning = outcome.ok ? outcome.warnings?.[0] : undefined;
			setStatus({
				kind: outcome.ok ? (warning ? "info" : "success") : "error",
				message: warning ? `${outcome.message} ⚠ ${warning}` : outcome.message,
				details: outcome.details,
			});
			editor.current?.highlight((outcome.ok ? outcome.warningBlockIds : outcome.blockIds) ?? []);
		} catch (error) {
			fail(error);
		} finally {
			setBusy(false);
		}
	};

	const toggleLive = async () => {
		if (live) {
			await bridge.liveStop().catch(fail);
			setLive(false);
			editor.current?.showRunning([]);
			return;
		}
		if (!port) {
			setStatus({ kind: "info", message: t("status.no_port") });
			return;
		}
		try {
			const started = await bridge.liveStart(currentProject(), port, language);
			if (!started.ok) {
				setStatus({ kind: "error", message: started.message ?? "" });
				return;
			}
			setLiveSnapshot(null);
			history.current = new Map();
			setLive(true);
		} catch (error) {
			fail(error);
		}
	};

	// While Live mode runs: show what the board does.
	useEffect(() => {
		if (!live) return;
		const timer = setInterval(() => {
			void bridge
				.liveState()
				.then((snapshot) => {
					const record = (key: string, value: number) => {
						const values = history.current.get(key) ?? [];
						values.push(value);
						if (values.length > historyLength) values.shift();
						history.current.set(key, values);
					};
					for (const [id, value] of Object.entries(snapshot.variables ?? {})) record(`var:${id}`, value);
					for (const [pin, value] of Object.entries(snapshot.readings ?? {})) record(`pin:${pin}`, value);
					setLiveSnapshot(snapshot);
					setVariableNames(
						new Map((editor.current?.save().variables ?? []).map((variable) => [variable.id, variable.name] as const))
					);
					editor.current?.showRunning(snapshot.running ?? []);
					if (snapshot.state === "failed") {
						setStatus({ kind: "error", message: snapshot.message ?? "", details: snapshot.details });
						editor.current?.highlight(snapshot.problems ?? []);
						setLive(false);
					} else if (snapshot.state === "stopped") {
						setLive(false);
					} else if (snapshot.message) {
						setStatus({ kind: "info", message: snapshot.message });
					}
				})
				.catch(fail);
		}, 200);
		return () => clearInterval(timer);
	}, [live, fail]);

	const liveRows = [
		...Object.entries(liveSnapshot?.variables ?? {}).map(([id, value]) => ({
			key: `var:${id}`,
			label: variableNames.get(id) ?? id,
			value,
		})),
		...Object.entries(liveSnapshot?.readings ?? {}).map(([pin, value]) => ({
			key: `pin:${pin}`,
			label: t("live.pin", { pin }),
			value,
		})),
	];

	return (
		<AppShell header={{ height: 64 }} padding={0}>
			<AppShell.Header px="md">
				<Group h="100%" gap="sm" wrap="nowrap">
					<img src={logo} alt="" width={36} height={36} style={{ borderRadius: 6, flexShrink: 0 }} />
					<Title order={4} visibleFrom="xl" style={{ fontFamily: "Space Mono, monospace", whiteSpace: "nowrap" }}>
						{t("app.title")}
					</Title>
					<TextInput
						aria-label={t("project.name")}
						value={name}
						onChange={(event) => {
							setName(event.currentTarget.value);
							markDirty();
						}}
						miw={120}
						maw={200}
						style={{ flex: 1 }}
					/>
					<ActionIcon.Group>
						<Tooltip label={t("toolbar.new")}>
							<ActionIcon variant="default" size="lg" aria-label={t("toolbar.new")} onClick={() => setGalleryShown(true)}>
								<IconFilePlus size={18} />
							</ActionIcon>
						</Tooltip>
						<Tooltip label={t("toolbar.open")}>
							<ActionIcon variant="default" size="lg" aria-label={t("toolbar.open")} onClick={() => void openProject()}>
								<IconFolderOpen size={18} />
							</ActionIcon>
						</Tooltip>
						<Menu position="bottom-start" onOpen={() => void loadRecent()} withinPortal>
							<Menu.Target>
								<Tooltip label={t("toolbar.recent")}>
									<ActionIcon variant="default" size="lg" aria-label={t("toolbar.recent")}>
										<IconHistory size={18} />
									</ActionIcon>
								</Tooltip>
							</Menu.Target>
							<Menu.Dropdown maw={420}>
								<Menu.Label>{t("toolbar.recent")}</Menu.Label>
								{recent.length === 0 && <Menu.Item disabled>{t("recent.none")}</Menu.Item>}
								{recent.map((entry) => (
									<Menu.Item key={entry.path} onClick={() => void openRecent(entry.path)}>
										<Text size="sm" fw={600} truncate>
											{entry.name}
										</Text>
										<Text size="xs" c="dimmed" truncate>
											{entry.path}
										</Text>
									</Menu.Item>
								))}
							</Menu.Dropdown>
						</Menu>
						<Tooltip label={`${t("toolbar.save")} (${shortcut("S")})`}>
							<ActionIcon variant="default" size="lg" aria-label={t("toolbar.save")} onClick={() => void saveProject()}>
								<IconDeviceFloppy size={18} />
							</ActionIcon>
						</Tooltip>
					</ActionIcon.Group>
					<Select
						aria-label={t("board.label")}
						data={boards.map((board) => ({ value: board.id, label: board.name }))}
						value={boardId}
						onChange={(value) => {
							if (value) {
								setBoardId(value);
								markDirty();
							}
						}}
						allowDeselect={false}
						miw={130}
						maw={190}
						style={{ flex: 1 }}
					/>
					<Select
						aria-label={t("port.label")}
						placeholder={t("port.none")}
						data={ports.map((candidate) => ({ value: candidate.address, label: candidate.label }))}
						value={port}
						onChange={setPort}
						miw={130}
						maw={240}
						style={{ flex: 1 }}
					/>
					<Tooltip label={t("port.refresh")}>
						<ActionIcon variant="default" size="lg" aria-label={t("port.refresh")} onClick={() => void refreshPorts()}>
							<IconRefresh size={18} />
						</ActionIcon>
					</Tooltip>
					<Group gap="xs" ml="auto" wrap="nowrap" style={{ flexShrink: 0 }}>
						<Tooltip label={t("monitor.toggle")}>
							<ActionIcon
								variant={monitorShown ? "filled" : "default"}
								size="lg"
								aria-label={t("monitor.toggle")}
								aria-pressed={monitorShown}
								onClick={() => setMonitorShown((shown) => !shown)}
							>
								<IconTerminal2 size={18} />
							</ActionIcon>
						</Tooltip>
						{example && (
							<Tooltip label={t("toolbar.wiring")}>
								<ActionIcon variant="default" size="lg" aria-label={t("toolbar.wiring")} onClick={() => setWiringShown(true)}>
									<IconPlugConnected size={18} />
								</ActionIcon>
							</Tooltip>
						)}
						<Tooltip label={t("toolbar.code")}>
							<ActionIcon variant="default" size="lg" aria-label={t("toolbar.code")} onClick={() => void showCode()}>
								<IconCode size={18} />
							</ActionIcon>
						</Tooltip>
						<Button
							color={live ? "pink" : "blue"}
							variant={live ? "filled" : "light"}
							leftSection={live ? <IconPlayerStop size={18} /> : <IconPlayerPlay size={18} />}
							onClick={() => void toggleLive()}
						>
							{t(live ? "toolbar.stop_live" : "toolbar.live")}
						</Button>
						<Button color="green" leftSection={<IconCheck size={18} />} loading={busy} disabled={live} onClick={() => void run("build")}>
							{t("toolbar.build")}
						</Button>
						<Button color="orange" leftSection={<IconUpload size={18} />} loading={busy} disabled={live} onClick={() => void run("upload")}>
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
						onLoadError={(lost) =>
							setStatus({ kind: "error", message: t("status.load_failed", { count: String(lost) }) })
						}
						onChange={() => {
							markDirty();
							if (!live) return;
							// Live mode: send the new blocks once the child stops dragging for a moment.
							if (liveUpdate.current) clearTimeout(liveUpdate.current);
							liveUpdate.current = setTimeout(() => void bridge.liveUpdate(currentProject()).catch(fail), 300);
						}}
					/>
				</div>
			</AppShell.Main>

			{((live && liveRows.length > 0) || monitorShown) && (
				<Stack gap="sm" style={{ position: "fixed", top: 80, right: 24, zIndex: 150 }}>
					{live && liveRows.length > 0 && (
						<Card withBorder shadow="sm" w={360}>
							<Text fw={700} size="sm" mb={4}>
								{t("live.title")}
							</Text>
							{liveRows.map((row) => (
								<Group key={row.key} justify="space-between" gap="sm" wrap="nowrap">
									<Text size="sm" truncate style={{ flex: 1 }}>
										{row.label}
									</Text>
									<Sparkline values={history.current.get(row.key) ?? []} label={row.label} />
									<Code style={{ minWidth: 56, textAlign: "right" }}>
										{Number.isInteger(row.value) ? row.value : row.value.toFixed(2)}
									</Code>
								</Group>
							))}
						</Card>
					)}
					{monitorShown && (
						<SerialMonitor
							t={t}
							language={language}
							port={port}
							live={live}
							liveLines={liveSnapshot?.output ?? []}
							busy={busy}
							onError={(message, details) => setStatus({ kind: "error", message, details })}
							onClose={() => setMonitorShown(false)}
						/>
					)}
				</Stack>
			)}

			{status && (
				// Over the workspace, between the palette and the zoom/trash controls.
				<div
					style={{
						position: "fixed",
						left: 330,
						right: 110,
						bottom: 16,
						zIndex: 150,
						maxHeight: "50vh",
						overflowY: "auto",
					}}
				>
					<Alert
						color={status.kind === "error" ? "orange" : status.kind === "success" ? "green" : "blue"}
						withCloseButton
						onClose={() => setStatus(null)}
						icon={busy ? <Loader size="xs" /> : undefined}
					>
						<Text size="sm" style={{ whiteSpace: "pre-wrap", wordBreak: "break-word" }}>
							{status.message}
						</Text>
						{status.details && (
							<Spoiler maxHeight={0} showLabel={t("status.details")} hideLabel={t("status.details")}>
								<ScrollArea.Autosize mah={160}>
									<Code block style={{ whiteSpace: "pre-wrap", wordBreak: "break-word" }}>
										{status.details}
									</Code>
								</ScrollArea.Autosize>
							</Spoiler>
						)}
					</Alert>
				</div>
			)}

			<Gallery
				opened={galleryShown}
				language={language}
				t={t}
				onBlank={newProject}
				onExample={openExample}
				onClose={() => setGalleryShown(false)}
			/>
			<Modal opened={restore !== null} onClose={discardRestore} title={t("restore.title")} centered>
				<Text mb="md">
					{restore &&
						t("restore.message", {
							name: restore.project.name,
							time: new Date(restore.savedAt * 1000).toLocaleString(language),
						})}
				</Text>
				<Group justify="flex-end">
					<Button variant="default" onClick={discardRestore}>
						{t("restore.discard")}
					</Button>
					<Button onClick={acceptRestore} data-autofocus>
						{t("restore.accept")}
					</Button>
				</Group>
			</Modal>
			<Wiring example={example} opened={wiringShown} language={language} t={t} onClose={() => setWiringShown(false)} />

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
