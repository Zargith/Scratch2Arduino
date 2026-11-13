import {
	ActionIcon,
	Alert,
	AppShell,
	Button,
	Card,
	Code,
	Group,
	Loader,
	LoadingOverlay,
	Menu,
	Modal,
	ScrollArea,
	SegmentedControl,
	Select,
	Spoiler,
	Stack,
	Text,
	TextInput,
	Tooltip,
} from "@mantine/core";
import {
	IconBluetooth,
	IconCheck,
	IconDeviceFloppy,
	IconFilePlus,
	IconFolderOpen,
	IconHelp,
	IconHistory,
	IconPlayerPause,
	IconPlayerPlay,
	IconPlayerTrackNext,
	IconPlayerStop,
	IconPlugConnected,
	IconRefresh,
	IconSchool,
	IconBooks,
	IconSettings,
	IconTerminal2,
	IconUpload,
	IconWifi,
} from "@tabler/icons-react";
import { useMediaQuery } from "@mantine/hooks";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";

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
	type Settings as SavedSettings,
	type SimState,
	type WiringPlan,
} from "@/bridge";
import { boards, defaultBoardId, findBoard, type Language } from "@/catalog";
import type { UserDeclaration } from "@/blocks/definitions";
import { CodeView, type CodeTab } from "@/CodeView";
import { Editor, type EditorHandle } from "@/Editor";
import { type DocPath, docsHome, tutorialFor } from "@/docs";
import { exampleProject, examples, type Example } from "@/examples";
import { Gallery } from "@/Gallery";
import { ArduinoLibraries } from "@/ArduinoLibraries";
import { Library } from "@/Library";
import { LiveWiring } from "@/LiveWiring";
import { ProjectWiring } from "@/ProjectWiring";
import { BluetoothDialog } from "@/BluetoothDialog";
import {
	bluetoothPort,
	bluetoothPorts,
	isBluetoothPort,
	reachableByBluetooth,
	rememberBoard,
	rememberedBoards,
} from "@/bluetooth";
import type { SavedGroup } from "@/myBlocks";
import logo from "@/logo.png";
import { SerialMonitor } from "@/SerialMonitor";
import { Settings } from "@/Settings";
import { isSimPort, SIM_PORT, usesPhone } from "@/simulation";
import { Simulator } from "@/Simulator";
import { useSavedColorScheme } from "@/theme/colorScheme";
import { findUpdate, platform, type Release } from "@/updates";
import { Tour } from "@/tour/Tour";
import { melodyCode, PROJECT_NAME, type TourContext, type TourStep } from "@/tour/steps";
import { Tutorials } from "@/Tutorials";
import { UnsavedChanges, type UnsavedChoice } from "@/UnsavedChanges";
import { WifiDialog } from "@/WifiDialog";
import { hasWifi, isWifiPort, rememberAddress, rememberedAddresses, wifiPort, wifiPorts } from "@/wifi";
import { Sparkline } from "@/Sparkline";
import {
	isScript,
	isStarterCode,
	isStarterUserCode,
	programNames,
	programSwitch,
	starterCode,
	starterUserCode,
	type CodeProblem,
} from "@/program";
import { detectLanguage, translate, type MessageKey } from "@/i18n";
import {
	emptyProject,
	projectToWorkspace,
	unknownBlockTypes,
	withProgram,
	withLibraries,
	withUserCode,
	workspaceToProject,
	type ProgramLanguage,
	type Project,
	type ProjectLibrary,
	type UserCode,
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

/** Name of a text language in messages ("C++", "Python"...). */
const programName = (program: ProgramLanguage, blocks: string): string =>
	program === "blocks" ? blocks : programNames[program];

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

	/** Blocks view, or code view (the code of the blocks and "My code"). */
	const [view, setView] = useState<"blocks" | "code">("blocks");
	const [codeTab, setCodeTab] = useState<CodeTab>("blocks");
	/** Shown over the window while a view is prepared ("Converting the blocks to code..."). */
	const [loading, setLoading] = useState<string | null>(null);
	/** The code of the blocks, for the code view. */
	const [blocksCode, setBlocksCode] = useState("");
	/** "My code": a state for its language, a ref for its text (the editor reports every key). */
	const [userCode, setUserCode] = useState<UserCode | null>(null);
	const userCodeRef = useRef<UserCode | null>(null);
	/** Its functions and variables, as blocks of the "My code" category. */
	const [declarations, setDeclarations] = useState<UserDeclaration[]>([]);
	/** Counts the "My code" scans: an older one (another project) never fills the palette. */
	const declarationsRequest = useRef(0);
	/** Mistakes of the last Check, Upload or Live: in "My code", and in an expert program. */
	const [userProblems, setUserProblems] = useState<CodeProblem[]>([]);
	const [programProblems, setProgramProblems] = useState<CodeProblem[]>([]);
	/** Language of the program: blocks, or code typed in the code editor (expert mode). */
	const [program, setProgram] = useState<ProgramLanguage>("blocks");
	/** The code of a text program: a ref, not a state, as the code editor reports every key. */
	const codeRef = useRef("");
	/** True once the user typed in the code: switching language then asks before replacing it. */
	const codeEdited = useRef(false);
	/** The expert program typed before going back to the blocks: offered again when coming back. */
	const lastExpert = useRef<{ language: ProgramLanguage; code: string } | null>(null);
	const [helpShown, setHelpShown] = useState(false);

	const [name, setName] = useState(() => translate(detectLanguage(), "project.untitled"));
	const [boardId, setBoardId] = useState(defaultBoardId);
	/** Narrow window: the Live, Check and Upload buttons show only their icon so that nothing overflows. */
	const compact = useMediaQuery("(max-width: 1600px)") ?? false;
	const [path, setPath] = useState("");
	const [dirty, setDirty] = useState(false);
	const [ports, setPorts] = useState<Port[]>([]);
	const [port, setPort] = useState<string | null>(null);
	/** Addresses of boards found on the home Wi-Fi (ESP32), offered as ports. */
	const [wifiAddresses, setWifiAddresses] = useState<string[]>(rememberedAddresses);
	const [wifiShown, setWifiShown] = useState(false);
	/** Boards found by a Bluetooth search (ESP32), offered as ports. */
	const [bluetoothBoards, setBluetoothBoards] = useState(rememberedBoards);
	const [bluetoothShown, setBluetoothShown] = useState(false);
	const [busy, setBusy] = useState(false);
	const [status, setStatus] = useState<Status | null>(null);
	const [live, setLive] = useState(false);
	const [liveSnapshot, setLiveSnapshot] = useState<LiveSnapshot | null>(null);
	/** Simulation (Live mode on the virtual board): what it shows, and the components of the blocks. */
	const [simState, setSimState] = useState<SimState | null>(null);
	const [simPlan, setSimPlan] = useState<WiringPlan | null>(null);
	/** The blocks use the Phone blocks: the virtual phone is shown. */
	const [simPhone, setSimPhone] = useState(false);
	/** The wiring hints of this Live session were closed by the user. */
	const [wiringClosed, setWiringClosed] = useState(false);
	/** Step-by-step mode of the Live mode (block programs). */
	const [livePaused, setLivePaused] = useState(false);
	/** The output panel opened by itself once in this Live session (at the first printed line). */
	const outputShown = useRef(false);
	/** Live message on screen: shown again only when it changes (once closed, it stays closed). */
	const liveMessage = useRef("");
	const [variableNames, setVariableNames] = useState<Map<string, string>>(new Map());
	const liveUpdate = useRef<ReturnType<typeof setTimeout> | null>(null);
	/** Last values of each variable ("var:<id>") and sensor ("pin:<label>"), for the graphs. */
	const history = useRef<Map<string, number[]>>(new Map());
	const [monitorShown, setMonitorShown] = useState(false);
	const [galleryShown, setGalleryShown] = useState(false);
	const [settingsShown, setSettingsShown] = useState(false);
	const [settings, setSettings] = useState<SavedSettings>({ language: "", checkUpdates: true, theme: "" });
	useSavedColorScheme(settings.theme);
	/** Installed version, told by the host ("" outside the application). */
	const [version, setVersion] = useState("");
	/** A newer release, offered until the user says "Later" (never forced). */
	const [update, setUpdate] = useState<Release | null>(null);
	/** Page of the Tutorials panel, null when it is closed. */
	const [tutorialPage, setTutorialPage] = useState<DocPath | null>(null);
	/** The example being edited: its wiring diagram stays one click away. */
	const [example, setExample] = useState<Example | null>(null);
	const [wiringShown, setWiringShown] = useState(false);
	const [recent, setRecent] = useState<RecentProject[]>([]);
	const [library, setLibrary] = useState<SavedGroup[]>([]);
	const [libraryShown, setLibraryShown] = useState(false);
	/** Arduino libraries of the project (library manager), installed by the Build when missing. */
	const [projectLibraries, setProjectLibraries] = useState<ProjectLibrary[]>([]);
	const [librariesShown, setLibrariesShown] = useState(false);
	/** Unsaved work found at start-up, offered back to the user. */
	const [restore, setRestore] = useState<AutosavedProject | null>(null);
	/** Interactive tutorial: the step shown, null when it is closed. */
	const [tourStep, setTourStep] = useState<number | null>(null);
	/** Changes of the blocks, counted while the tutorial is open (its steps look at the project). */
	const [tourEdits, setTourEdits] = useState(0);
	/** Successful checks and uploads: a step of the tutorial waits for one. */
	const [checks, setChecks] = useState(0);
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

	/** The blocks of the project (still kept while the program is written as text). */
	const currentBlocks = useCallback(
		(): Project =>
			workspaceToProject(editor.current?.save() ?? initialState.current, {
				name,
				board: boardId,
				initialValues: initialValues.current,
			}),
		[name, boardId]
	);

	const currentProject = useCallback(
		(): Project =>
			withLibraries(withUserCode(withProgram(currentBlocks(), program, codeRef.current), userCodeRef.current), projectLibraries),
		[currentBlocks, program, projectLibraries]
	);

	/** Shows the program of an opened project: its blocks, and its code when written as text. */
	const loadProgram = useCallback((project: Project) => {
		const language = project.language ?? "blocks";
		setProgram(language);
		codeRef.current = language === "blocks" ? "" : (project.code ?? "");
		codeEdited.current = language !== "blocks";
		userCodeRef.current = project.userCode ? { ...project.userCode } : null;
		setUserCode(userCodeRef.current);
		setProjectLibraries(project.libraries ?? []);
		setUserProblems([]);
		setProgramProblems([]);
		setView(language === "blocks" ? "blocks" : "code");
		setCodeTab("blocks");
		setDeclarations([]);
		const request = ++declarationsRequest.current;
		const found = project.userCode;
		if (found && hasHost()) {
			bridge
				.codeDeclarations(found.language, found.code)
				.catch(() => [])
				.then((list) => {
					if (request === declarationsRequest.current) setDeclarations(list);
				});
		}
	}, []);

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

	// A board without Wi-Fi cannot use a Wi-Fi port: back to the first USB port.
	useEffect(() => {
		if (!hasWifi(boardId) && isWifiPort(port)) setPort(ports[0]?.address ?? SIM_PORT);
		if (!reachableByBluetooth(boardId) && port?.startsWith("bt:")) setPort(ports[0]?.address ?? SIM_PORT);
	}, [boardId, port, ports]);

	const refreshPorts = useCallback(async () => {
		if (!hasHost()) return;
		try {
			const found = await bridge.ports();
			setPorts(found);
			// A Wi-Fi or Bluetooth port, or the virtual board, stays chosen: it is not in the list of
			// plugged-in boards. Without any board, the virtual one is chosen.
			setPort((current) =>
				current &&
				(isWifiPort(current) ||
					current.startsWith("bt:") ||
					(isSimPort(current) && found.length === 0) ||
					found.some((candidate) => candidate.address === current))
					? current
					: (found[0]?.address ?? SIM_PORT)
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
		void bridge.setTitle(`${dirty ? "* " : ""}${file} - ${t("app.title")}`).catch(() => undefined);
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

	/** Applies and saves the settings (outside the application they only last until it closes). */
	const changeSettings = (next: SavedSettings) => {
		setSettings(next);
		if (hasHost()) void bridge.saveSettings(next).catch(() => undefined);
	};

	// Once, at start-up: the host tells the system language. Later language changes are the
	// user's (the FR/EN switch) and must not be overridden.
	const startup = useRef({ fail, refreshPorts });
	useEffect(() => {
		if (!hasHost()) return;
		void bridge
			.hello()
			.then(async (info) => {
				switchLanguageRef.current(info.language);
				setVersion(info.version);
				const saved = await bridge.settings().catch(() => null);
				if (!saved) return;
				setSettings(saved);
				// In the background: the editor is usable at once, and no network is no problem.
				if (saved.checkUpdates) {
					void findUpdate(info.version)
						.then((release) => {
							if (release) setUpdate(release);
						})
						.catch(() => undefined);
				}
			})
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
		loadProgram(starterProject(untitled));
		reload(projectToWorkspace(starterProject(untitled)));
	};

	/** Starts the interactive tutorial on a new project (the current one is kept if the user says no). */
	const startTour = () => {
		if (dirty && !window.confirm(t("confirm.new"))) return;
		setGalleryShown(false);
		setTutorialPage(null);
		const projectName = PROJECT_NAME[language];
		initialValues.current = {};
		setName(projectName);
		setPath("");
		setDirty(false);
		setStatus(null);
		setExample(null);
		forgetAutosave();
		loadProgram(starterProject(projectName));
		reload(projectToWorkspace(starterProject(projectName)));
		if (view === "code") void showBlocksView();
		setTourEdits(0);
		setTourStep(0);
	};

	/** The button of a tutorial step: writes the melodie() function in "My code" (C++) and shows it. */
	const tourAction = (action: NonNullable<TourStep["action"]>["run"]) => {
		if (action !== "insert-melody") return;
		const current = userCodeRef.current;
		const kept = current && current.language === "cpp" && !isStarterCode(current.code) && !isStarterUserCode(current.code);
		const code = melodyCode(language);
		userCodeRef.current = { language: "cpp", code: kept ? `${current.code.trimEnd()}\n\n${code}` : code };
		setUserCode(userCodeRef.current);
		setUserProblems([]);
		markDirty();
		void showCodeView("mine");
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
			loadProgram(opened.project);
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
				return Boolean(opened);
			})
			.catch((error: unknown) => {
				fail(error);
				return true;
			})
			.then(async (projectGiven) => {
				const saved = await found;
				if (saved) setRestore(saved);
				// First start (no project given, nothing to recover, no recent project): the tutorials.
				else if (!projectGiven && (await bridge.recent().catch(() => null))?.length === 0) setTutorialPage(docsHome);
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
			if (!saved) return false;
			setPath(saved);
			autosavedEdits.current = savedEdits; // the host dropped the autosave
			if (edits.current === savedEdits) setDirty(false);
			setStatus({ kind: "success", message: t("status.saved", { path: saved }) });
			return true;
		} catch (error) {
			fail(error);
			return false;
		}
	};

	/** Asked before the update installer closes Scratch2Arduino: save, ignore or cancel. */
	const [unsavedAsked, setUnsavedAsked] = useState(false);
	const unsavedAnswer = useRef<((choice: UnsavedChoice) => void) | null>(null);
	/** True when Scratch2Arduino may close now (nothing to save, saved, or changes ignored). */
	const readyToClose = async () => {
		if (!dirty) return true;
		const choice = await new Promise<UnsavedChoice>((resolve) => {
			unsavedAnswer.current = resolve;
			setUnsavedAsked(true);
		});
		if (choice === "cancel") return false;
		return choice === "discard" || (await saveProject());
	};
	const answerUnsaved = (choice: UnsavedChoice) => {
		setUnsavedAsked(false);
		unsavedAnswer.current?.(choice);
		unsavedAnswer.current = null;
	};
	/** Only the Windows installer closes the program; elsewhere the link is just opened. */
	const installCloses = (download: string) => platform(navigator.userAgent) === "windows" && download.endsWith(".exe");

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

	// "My blocks" is shared by all projects: read once, then kept in sync with each change.
	useEffect(() => {
		if (!hasHost()) return;
		bridge
			.library()
			.then(setLibrary)
			.catch(() => setLibrary([]));
	}, []);

	const saveGroup = async (blocks: Project, groupName: string) => {
		try {
			const added = await bridge.libraryAdd(groupName, blocks);
			setLibrary((groups) => [...groups, added]);
			setStatus({ kind: "success", message: t("status.library_saved", { name: added.name }) });
		} catch (error) {
			fail(error);
		}
	};

	const renameGroup = async (group: SavedGroup, groupName: string) => {
		try {
			await bridge.libraryRename(group.id, groupName);
			setLibrary((groups) => groups.map((candidate) => (candidate.id === group.id ? { ...candidate, name: groupName } : candidate)));
		} catch (error) {
			fail(error);
		}
	};

	const removeGroup = async (group: SavedGroup) => {
		try {
			await bridge.libraryRemove(group.id);
			setLibrary((groups) => groups.filter((candidate) => candidate.id !== group.id));
		} catch (error) {
			fail(error);
		}
	};

	const exportLibrary = async () => {
		try {
			const file = await bridge.libraryExport(language);
			if (file) setStatus({ kind: "success", message: t("status.library_exported", { path: file }) });
		} catch (error) {
			fail(error);
		}
	};

	const importLibrary = async () => {
		try {
			const count = await bridge.libraryImport(language);
			if (count === null) return;
			setLibrary(await bridge.library());
			setStatus({ kind: "success", message: t("status.library_imported", { count: String(count) }) });
		} catch (error) {
			fail(error);
		}
	};

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
		loadProgram(project);
		reload(projectToWorkspace(project));
		setStatus({ kind: "success", message: t("status.restored", { name: project.name }) });
	};

	const discardRestore = () => {
		setRestore(null);
		if (!dirty) forgetAutosave();
	};

	/** Lets the overlay be painted before a long synchronous step (Blockly building the blocks). */
	const nextFrame = () => new Promise<void>((resolve) => requestAnimationFrame(() => setTimeout(resolve, 0)));

	/** Code view: the code of the blocks is made again, the blocks are kept aside. */
	const showCodeView = async (tab?: CodeTab) => {
		if (tab) setCodeTab(tab);
		if (view === "code") return;
		setLoading(t("view.loading_code"));
		try {
			await nextFrame();
			initialState.current = editor.current?.save() ?? initialState.current;
			let made = "";
			if (hasHost()) {
				try {
					made = await bridge.generate(currentBlocks());
				} catch (error) {
					fail(error);
				}
			}
			setBlocksCode(made);
			setView("code");
		} finally {
			setLoading(null);
		}
	};

	/** Blocks view: "My code" is read again for its palette category, then the blocks are built. */
	const showBlocksView = async () => {
		if (view === "blocks" || program !== "blocks") return;
		setLoading(t("view.loading_blocks"));
		try {
			const mine = userCodeRef.current;
			const request = ++declarationsRequest.current;
			const list = mine && hasHost() ? await bridge.codeDeclarations(mine.language, mine.code).catch(() => []) : [];
			if (request === declarationsRequest.current) setDeclarations(list);
			await nextFrame();
			setView("blocks");
			await nextFrame(); // the blocks are built while the overlay is shown
		} finally {
			setLoading(null);
		}
	};

	/** Creates, switches or deletes "My code"; typed code is only lost after a confirmation. */
	/** Tells what a conversion did (and which lines are left to the user). */
	const showConverted = (next: Exclude<ProgramLanguage, "blocks">, toCheck: number[]) => {
		setStatus(
			toCheck.length > 0
				? { kind: "info", message: t("status.converted_check", { language: programNames[next], lines: toCheck.join(", ") }) }
				: { kind: "success", message: t("status.converted", { language: programNames[next] }) }
		);
	};

	/** "My code" in another language: typed code is converted (after a yes), an untouched example is replaced. */
	const changeUserLanguage = async (next: Exclude<ProgramLanguage, "blocks"> | null) => {
		const current = userCodeRef.current;
		if (current && next === current.language) return;
		const typed = current !== null && !isStarterCode(current.code) && !isStarterUserCode(current.code);
		let code = next === null ? "" : starterUserCode(next, language);
		if (next === null) {
			if (!window.confirm(t("code.confirm_remove"))) return;
		} else if (current && typed) {
			if (!window.confirm(t("code.confirm_language", { from: programNames[current.language], to: programNames[next] }))) {
				return;
			}
			if (!hasHost()) {
				setStatus({ kind: "info", message: t("status.no_host") });
				return;
			}
			try {
				const converted = await bridge.convert(current.code, current.language, next);
				code = converted.code;
				showConverted(next, converted.toCheck);
			} catch (error) {
				fail(error);
				return;
			}
		}
		userCodeRef.current = next === null ? null : { language: next, code };
		setUserCode(userCodeRef.current);
		setUserProblems([]);
		markDirty();
	};

	/**
	 * Writes the program in another language. The blocks are always kept; typed code is only
	 * replaced or deleted after the user said yes.
	 */
	const switchProgram = async (next: ProgramLanguage) => {
		if (next === program || live) return;
		setProgramProblems([]);
		const typed = codeEdited.current && !isStarterCode(codeRef.current);
		const blocksName = t("program.blocks");
		if (next === "blocks") {
			if (typed && !window.confirm(t("program.confirm_blocks", { language: programName(program, blocksName) }))) return;
			// Kept for this session: going back to the expert mode offers it again.
			lastExpert.current = typed ? { language: program, code: codeRef.current } : null;
			codeRef.current = "";
			setProgram("blocks");
			markDirty();
			setStatus({ kind: "info", message: t("status.program_blocks") });
			return;
		}
		const change = programSwitch(program, codeRef.current, typed);
		if (change.action === "convert") {
			// A program is converted (after a yes when it was typed), never thrown away: also the C++
			// made from the blocks or the code of an opened project.
			if (
				change.confirm &&
				!window.confirm(t("program.confirm_replace", { from: programName(program, blocksName), to: programNames[next] }))
			) {
				return;
			}
			if (!hasHost()) {
				setStatus({ kind: "info", message: t("status.no_host") });
				return;
			}
			try {
				const converted = await bridge.convert(codeRef.current, program, next);
				codeRef.current = converted.code;
				codeEdited.current = change.confirm; // still untouched when the code came from the blocks
				setProgram(next);
				markDirty();
				showConverted(next, converted.toCheck);
			} catch (error) {
				fail(error);
			}
			return;
		}
		if (program === "blocks") initialState.current = editor.current?.save() ?? initialState.current;
		const previous = lastExpert.current;
		if (previous && previous.language === next && window.confirm(t("program.confirm_restore", { language: programNames[next] }))) {
			setView("code");
			codeRef.current = previous.code;
			codeEdited.current = true;
			setProgram(next);
			markDirty();
			setStatus({ kind: "info", message: t("status.program_restored") });
			return;
		}
		setView("code");
		let start = starterCode(next, language);
		let fromBlocks = false;
		let toCheck: number[] = [];
		// The blocks already are a program: with "My code" first (its functions are used by the
		// blocks), it is the best starting point. In C++ as it is, in Python or JavaScript
		// converted from that C++ (no need to go through the C++ mode first).
		if (hasHost()) {
			try {
				const sketch = await bridge.expertSketch(withUserCode(currentBlocks(), userCodeRef.current));
				if (next === "cpp") {
					start = sketch;
				} else {
					const converted = await bridge.convert(sketch, "cpp", next);
					start = converted.code;
					toCheck = converted.toCheck;
				}
				fromBlocks = true;
			} catch {
				// keep the example
			}
		}
		codeRef.current = start;
		codeEdited.current = false;
		setProgram(next);
		markDirty();
		const scriptKey = !fromBlocks
			? "status.program_script"
			: toCheck.length > 0
				? "status.program_script_from_blocks_check"
				: "status.program_script_from_blocks";
		setStatus({
			kind: "info",
			message:
				next === "cpp"
					? t(fromBlocks ? "status.program_cpp_from_blocks" : "status.program_cpp")
					: t(scriptKey, { language: programNames[next] }),
		});
	};

	/** Marks the faulty lines (after Check, Upload or Live): in "My code" or in the expert program. */
	const showProblems = (problems: CodeProblem[] | undefined) => {
		const all = problems ?? [];
		setUserProblems(all.filter((problem) => problem.file === "user"));
		setProgramProblems(all.filter((problem) => problem.file !== "user"));
		if (all.some((problem) => problem.file === "user") && view === "code") setCodeTab("mine");
	};

	const run = async (kind: "build" | "upload") => {
		if (kind === "upload" && isScript(program)) {
			setStatus({ kind: "info", message: t("status.upload_script", { language: programName(program, "") }) });
			return;
		}
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
				message: warning ? `${outcome.message}\n${warning}` : outcome.message,
				details: outcome.details,
			});
			if (outcome.ok) setChecks((count) => count + 1);
			if (program === "blocks") editor.current?.highlight((outcome.ok ? outcome.warningBlockIds : outcome.blockIds) ?? []);
			showProblems(outcome.problems);
		} catch (error) {
			fail(error);
		} finally {
			setBusy(false);
		}
	};

	/** Simulation: the components the blocks use, drawn on the virtual board. */
	const refreshSimPlan = () => {
		const project = currentProject();
		setSimPhone(usesPhone(JSON.stringify(project)));
		void bridge
			.wiring(project, language)
			.then(setSimPlan)
			.catch(() => setSimPlan(null)); // half-built blocks: the parts already drawn stay until the next change
	};
	const simulated = live && isSimPort(port);
	// While the virtual board runs: follow its outputs (faster than the Live panel, for blinking LEDs).
	useEffect(() => {
		if (!simulated) return;
		const timer = setInterval(() => {
			void bridge
				.simState()
				.then(setSimState)
				.catch(() => undefined);
		}, 100);
		return () => clearInterval(timer);
	}, [simulated]);

	const toggleLive = async () => {
		if (live) {
			await bridge.liveStop().catch(fail);
			setLive(false);
			editor.current?.showRunning([]);
			return;
		}
		if (program === "cpp") {
			setStatus({ kind: "info", message: t("status.live_cpp") });
			return;
		}
		if (!port) {
			setStatus({ kind: "info", message: t("status.no_port") });
			return;
		}
		showProblems([]);
		try {
			const started = await bridge.liveStart(currentProject(), port, language);
			if (!started.ok) {
				setStatus({ kind: "error", message: started.message ?? "" });
				return;
			}
			setLiveSnapshot(null);
			setSimState(null);
			if (isSimPort(port)) refreshSimPlan();
			setLivePaused(false);
			setWiringClosed(false);
			outputShown.current = false;
			history.current = new Map();
			setLive(true);
		} catch (error) {
			fail(error);
		}
	};

	// While Live mode runs: show what the board does.
	useEffect(() => {
		if (!live) return;
		liveMessage.current = "";
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
					// The program printed something (print, printError, an error): show it, once.
					if ((snapshot.outputCount ?? 0) > 0 && !outputShown.current) {
						outputShown.current = true;
						setMonitorShown(true);
					}
					setVariableNames(
						new Map((editor.current?.save().variables ?? []).map((variable) => [variable.id, variable.name] as const))
					);
					editor.current?.showRunning(snapshot.running ?? []);
					if (snapshot.state === "failed") {
						setStatus({ kind: "error", message: snapshot.message ?? "", details: snapshot.details });
						editor.current?.highlight(snapshot.problems ?? []);
						if (snapshot.line) showProblems([{ line: snapshot.line, message: snapshot.message ?? "", file: snapshot.file }]);
						setLive(false);
					} else if (snapshot.state === "stopped") {
						setLive(false);
					} else {
						const message = snapshot.paused && snapshot.state === "running" ? t("live.paused") : (snapshot.message ?? "");
						if (message && message !== liveMessage.current) setStatus({ kind: "info", message });
						liveMessage.current = message;
					}
				})
				.catch(fail);
		}, 200);
		return () => clearInterval(timer);
		// t: the status follows the language at the next poll anyway.
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, [live, fail]);

	/** Step-by-step mode: pause or let the blocks run again. */
	const toggleLivePause = useCallback(() => {
		const next = !livePaused;
		setLivePaused(next);
		void bridge.livePause(next).catch(fail);
	}, [livePaused, fail]);
	const liveStep = useCallback(() => {
		if (livePaused) void bridge.liveStep().catch(fail);
	}, [livePaused, fail]);
	/** Shows the step-by-step buttons: a block program (or blocks with "My code") in Live mode. */
	const canStep = live && program === "blocks";
	// F9 pauses or resumes, F10 runs the next block (as in many debuggers).
	useEffect(() => {
		if (!canStep) return;
		const onKey = (event: KeyboardEvent) => {
			if (event.key === "F9") {
				event.preventDefault();
				toggleLivePause();
			} else if (event.key === "F10") {
				event.preventDefault();
				liveStep();
			}
		};
		document.addEventListener("keydown", onKey);
		return () => document.removeEventListener("keydown", onKey);
	}, [canStep, toggleLivePause, liveStep]);

	/** The language the help window explains: the expert program, or "My code". */
	const helpLanguage: ProgramLanguage = program !== "blocks" ? program : (userCode?.language ?? "cpp");

	const wiringHints = live && !wiringClosed ? (liveSnapshot?.wiring ?? []) : [];
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

	// What the tutorial's steps look at: worked out again when the blocks or the editor change.
	// eslint-disable-next-line react-hooks/exhaustive-deps
	const tourContext = useMemo<TourContext | null>(
		() =>
			tourStep === null
				? null
				: {
						project: currentProject(),
						view,
						wiringShown,
						live,
						simulated: isSimPort(port),
						pressed: Object.entries(simState?.pressed ?? {})
							.filter(([, held]) => held)
							.map(([pin]) => pin),
						userCode,
						checks,
					},
		[tourStep, tourEdits, view, wiringShown, live, port, simState, userCode, checks, currentProject]
	);

	/** The column of panels on the right (virtual board, live values, serial monitor...) is shown. */
	const sidePanels = (live && liveRows.length > 0) || simulated || wiringHints.length > 0 || monitorShown || tutorialPage !== null;

	return (
		<AppShell header={{ height: 64 }} padding={0}>
			<AppShell.Header px="md">
				<Group h="100%" gap="sm" wrap="nowrap">
					{/* The name of the tool is in the window title: the logo is enough here. */}
					<img src={logo} alt={t("app.title")} width={36} height={36} style={{ borderRadius: 6, flexShrink: 0 }} />
					<TextInput
						aria-label={t("project.name")}
						value={name}
						onChange={(event) => {
							setName(event.currentTarget.value);
							markDirty();
						}}
						miw={80}
						maw={200}
						style={{ flex: 1, minWidth: 80 }}
					/>
					<ActionIcon.Group style={{ flexShrink: 0 }}>
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
							<Tooltip label={t("toolbar.recent")}>
								<Menu.Target>
									<ActionIcon variant="default" size="lg" aria-label={t("toolbar.recent")}>
										<IconHistory size={18} />
									</ActionIcon>
								</Menu.Target>
							</Tooltip>
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
					<SegmentedControl
						aria-label={t("view.label")}
						data={[
							{ value: "blocks", label: t("view.blocks"), disabled: program !== "blocks" || live },
							{ value: "code", label: t("view.code"), disabled: live },
						]}
						value={view}
						onChange={(value) => void (value === "code" ? showCodeView() : showBlocksView())}
					/>
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
						miw={100}
						maw={190}
						style={{ flex: 1, minWidth: 100 }}
					/>
					<Select
						aria-label={t("port.label")}
						placeholder={t("port.none")}
						data={[
							...ports,
							...wifiPorts(boardId, wifiAddresses, {
								accessPoint: t("port.wifi_access_point"),
								home: t("port.wifi_home", { address: "{address}" }),
							}),
							...bluetoothPorts(boardId, bluetoothBoards, t("port.bluetooth", { name: "{name}" })),
							{ address: SIM_PORT, label: t("port.simulation") },
						].map((candidate) => ({ value: candidate.address, label: candidate.label }))}
						value={port}
						onChange={setPort}
						miw={100}
						maw={240}
						style={{ flex: 1, minWidth: 100 }}
					/>
					<Tooltip label={t("port.refresh")}>
						<ActionIcon variant="default" size="lg" aria-label={t("port.refresh")} onClick={() => void refreshPorts()}>
							<IconRefresh size={18} />
						</ActionIcon>
					</Tooltip>
					<Group gap="xs" ml="auto" wrap="nowrap" style={{ flexShrink: 0 }}>
						<Tooltip label={t("toolbar.tutorials")}>
							<ActionIcon
								variant={tutorialPage !== null ? "filled" : "default"}
								size="lg"
								aria-label={t("toolbar.tutorials")}
								aria-pressed={tutorialPage !== null}
								onClick={() => setTutorialPage((page) => (page === null ? docsHome : null))}
							>
								<IconSchool size={18} />
							</ActionIcon>
						</Tooltip>
						<Tooltip label={t("libraries.title")}>
							<ActionIcon variant="default" size="lg" aria-label={t("libraries.title")} onClick={() => setLibrariesShown(true)}>
								<IconBooks size={18} />
							</ActionIcon>
						</Tooltip>
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
						{hasWifi(boardId) && (
							<Tooltip label={t("wifi.title")}>
								<ActionIcon variant="default" size="lg" aria-label={t("wifi.title")} onClick={() => setWifiShown(true)}>
									<IconWifi size={18} />
								</ActionIcon>
							</Tooltip>
						)}
						{reachableByBluetooth(boardId) && (
							<Tooltip label={t("bluetooth.title")}>
								<ActionIcon
									variant="default"
									size="lg"
									aria-label={t("bluetooth.title")}
									onClick={() => setBluetoothShown(true)}
								>
									<IconBluetooth size={18} />
								</ActionIcon>
							</Tooltip>
						)}
						{/* The wiring diagram in both views: it is computed from the blocks, kept in the code view too. */}
						<Tooltip label={t("toolbar.wiring")}>
							<ActionIcon variant="default" size="lg" aria-label={t("toolbar.wiring")} onClick={() => setWiringShown(true)}>
								<IconPlugConnected size={18} />
							</ActionIcon>
						</Tooltip>
						{/* The code of the blocks: the Blocks / Code switch (no button of its own any more). */}
						{view === "code" && (
							<Tooltip label={t("program.help")}>
								<ActionIcon variant="default" size="lg" aria-label={t("program.help")} onClick={() => setHelpShown(true)}>
									<IconHelp size={18} />
								</ActionIcon>
							</Tooltip>
						)}
						{/* On a narrow window these three buttons keep only their icon (the name in a tooltip). */}
						<Tooltip label={t(live ? "toolbar.stop_live" : "toolbar.live")} disabled={!compact}>
							<Button
								color={live ? "pink" : "blue"}
								variant={live ? "filled" : "light"}
								leftSection={compact ? undefined : live ? <IconPlayerStop size={18} /> : <IconPlayerPlay size={18} />}
								aria-label={t(live ? "toolbar.stop_live" : "toolbar.live")}
								px={compact ? "xs" : undefined}
								disabled={program === "cpp"}
								onClick={() => void toggleLive()}
							>
								{compact ? (
									live ? <IconPlayerStop size={18} /> : <IconPlayerPlay size={18} />
								) : (
									t(live ? "toolbar.stop_live" : "toolbar.live")
								)}
							</Button>
						</Tooltip>
						{canStep && (
							<ActionIcon.Group>
								<Tooltip label={`${t(livePaused ? "live.resume" : "live.pause")} (F9)`}>
									<ActionIcon
										variant={livePaused ? "filled" : "default"}
										color="pink"
										size="lg"
										aria-label={t(livePaused ? "live.resume" : "live.pause")}
										aria-pressed={livePaused}
										onClick={toggleLivePause}
									>
										{livePaused ? <IconPlayerPlay size={18} /> : <IconPlayerPause size={18} />}
									</ActionIcon>
								</Tooltip>
								<Tooltip label={`${t("live.next_block")} (F10)`}>
									<ActionIcon
										variant="default"
										size="lg"
										aria-label={t("live.next_block")}
										disabled={!livePaused}
										onClick={liveStep}
									>
										<IconPlayerTrackNext size={18} />
									</ActionIcon>
								</Tooltip>
							</ActionIcon.Group>
						)}
						<Tooltip label={t("toolbar.build")} disabled={!compact}>
							<Button
								color="green"
								leftSection={compact ? undefined : <IconCheck size={18} />}
								aria-label={t("toolbar.build")}
								px={compact ? "xs" : undefined}
								loading={busy}
								disabled={live}
								onClick={() => void run("build")}
							>
								{compact ? <IconCheck size={18} /> : t("toolbar.build")}
							</Button>
						</Tooltip>
						<Tooltip label={t("toolbar.upload")} disabled={!compact}>
							<Button
								color="orange"
								leftSection={compact ? undefined : <IconUpload size={18} />}
								aria-label={t("toolbar.upload")}
								px={compact ? "xs" : undefined}
								loading={busy}
								disabled={live || isScript(program)}
								onClick={() => void run("upload")}
							>
								{compact ? <IconUpload size={18} /> : t("toolbar.upload")}
							</Button>
						</Tooltip>
						<Tooltip label={t("settings.title")}>
							<ActionIcon variant="default" size="lg" aria-label={t("settings.title")} onClick={() => setSettingsShown(true)}>
								<IconSettings size={18} />
							</ActionIcon>
						</Tooltip>
					</Group>
				</Group>
			</AppShell.Header>

			<AppShell.Main style={{ position: "relative", height: "100dvh" }}>
				<LoadingOverlay
					visible={loading !== null}
					zIndex={300}
					overlayProps={{ blur: 1 }}
					loaderProps={{
						children: (
							<Stack align="center" gap="sm">
								<Loader />
								<Text fw={600}>{loading}</Text>
							</Stack>
						),
					}}
				/>
				<div style={{ position: "absolute", inset: "64px 0 0 0" }}>
					{view === "code" ? (
						<CodeView
							t={t}
							language={language}
							editorKey={editorKey}
							tab={codeTab}
							onTab={setCodeTab}
							program={program}
							onProgram={(next) => void switchProgram(next)}
							programCode={codeRef.current}
							onProgramCode={(typed) => {
								codeRef.current = typed;
								codeEdited.current = true;
								markDirty();
							}}
							programProblems={programProblems}
							generated={blocksCode}
							// The ref, not the state: it holds what was typed since (the editor remounts on each tab or view switch).
							userCode={userCodeRef.current}
							onUserCode={(typed) => {
								if (userCodeRef.current) userCodeRef.current = { ...userCodeRef.current, code: typed };
								markDirty();
							}}
							onUserLanguage={changeUserLanguage}
							userProblems={userProblems}
							disabled={live}
						/>
					) : (
						<Editor
							key={editorKey}
							ref={editor}
							language={language}
							board={findBoard(boardId)}
							initialState={initialState.current}
							library={library}
							onSaveGroup={(blocks, groupName) => void saveGroup(blocks, groupName)}
							onManageLibrary={() => setLibraryShown(true)}
							userDeclarations={declarations}
							onOpenCode={() => void showCodeView("mine")}
							onLoadError={(lost) =>
								setStatus({ kind: "error", message: t("status.load_failed", { count: String(lost) }) })
							}
							onChange={() => {
								markDirty();
								if (tourStep !== null) setTourEdits((count) => count + 1);
								if (!live) return;
								// Live mode: send the new blocks once the child stops dragging for a moment.
								if (liveUpdate.current) clearTimeout(liveUpdate.current);
								liveUpdate.current = setTimeout(() => {
									void bridge.liveUpdate(currentProject()).catch(fail);
									if (isSimPort(port)) refreshSimPlan();
								}, 300);
							}}
						/>
					)}
				</div>
			</AppShell.Main>

			{sidePanels && (
				// One column on the right: never taller than the window, it scrolls when the panels need more.
				<Stack
					gap="sm"
					style={{ position: "fixed", top: 80, right: 24, zIndex: 150, maxHeight: "calc(100dvh - 96px)", overflowY: "auto", paddingBottom: 4 }}
				>
					{simulated && (
						<Simulator
							maxHeight={(live && liveRows.length > 0) || monitorShown || wiringHints.length > 0 ? "28dvh" : "calc(60dvh - 90px)"}
							plan={simPlan}
							state={simState}
							phone={simPhone}
							t={t}
							onInput={(...input) => void bridge.simInput(...input).catch(fail)}
						/>
					)}
					{tutorialPage !== null && (
						<Tutorials
							language={language}
							t={t}
							page={tutorialPage}
							height={simulated ? "30dvh" : (live && liveRows.length > 0) || monitorShown ? "45dvh" : "calc(100dvh - 104px)"}
							onNavigate={(page, pageLanguage) => {
								if (pageLanguage !== language) switchLanguage(pageLanguage);
								setTutorialPage(page);
							}}
							onOpenExample={(exampleId) => {
								const chosen = examples.find((candidate) => candidate.group === exampleId);
								if (chosen) openExample(chosen);
							}}
							onClose={() => setTutorialPage(null)}
							onStartTour={startTour}
						/>
					)}
					{wiringHints.length > 0 && (
						<LiveWiring
							hints={wiringHints}
							t={t}
							onShowBlocks={(blockIds) => {
								if (view !== "blocks") void showBlocksView();
								editor.current?.highlight(blockIds);
							}}
							onClose={() => setWiringClosed(true)}
						/>
					)}
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
						left: view === "blocks" ? 330 : 64,
						// Beside the panels on the right (live values, virtual board...), not over them.
						right: sidePanels ? 400 : 110,
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

			{tourStep !== null && tourContext && (
				<Tour
					language={language}
					context={tourContext}
					step={tourStep}
					onStep={setTourStep}
					onAction={tourAction}
					onClose={() => setTourStep(null)}
				/>
			)}
			<Settings
				opened={settingsShown}
				language={language}
				t={t}
				version={version}
				board={boards.find((candidate) => candidate.id === boardId)?.name ?? boardId}
				settings={settings}
				onLanguage={(next) => {
					switchLanguage(next);
					changeSettings({ ...settings, language: next });
				}}
				onSettings={changeSettings}
				onUpdate={setUpdate}
				readyToClose={readyToClose}
				onClose={() => setSettingsShown(false)}
			/>
			<UnsavedChanges opened={unsavedAsked} t={t} onChoice={answerUnsaved} />
			{update && (
				<Card
					withBorder
					shadow="md"
					padding="sm"
					w={460}
					style={{ position: "fixed", top: 76, left: "50%", transform: "translateX(-50%)", zIndex: 200 }}
				>
					<Text size="sm" fw={600}>
						{t("update.available", { version: update.version, current: version })}
					</Text>
					<Text size="xs" c="dimmed" mt={4}>
						{t("update.help")}
					</Text>
					<Group gap="xs" mt="sm" justify="flex-end">
						<Button size="xs" variant="subtle" color="gray" onClick={() => setUpdate(null)}>
							{t("update.later")}
						</Button>
						<Button size="xs" variant="light" onClick={() => void bridge.openUrl(update.page).catch(fail)}>
							{t("update.notes")}
						</Button>
						<Button
							size="xs"
							onClick={() =>
								void (async () => {
									if (installCloses(update.download) && !(await readyToClose())) return;
									const started = await bridge.installUpdate(update.download, language);
									if (!started) await bridge.openUrl(update.download);
								})().catch(fail)
							}
						>
							{t(update.download.endsWith(".exe") ? "settings.install_update" : "update.download")}
						</Button>
					</Group>
				</Card>
			)}
			<Gallery
				opened={galleryShown}
				language={language}
				t={t}
				onBlank={newProject}
				onExample={openExample}
				onTutorial={(group) => {
					const tutorial = tutorialFor(group);
					if (!tutorial) return;
					setGalleryShown(false);
					setTutorialPage(tutorial.path);
				}}
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
			<WifiDialog
				opened={wifiShown}
				language={language}
				boardId={boardId}
				usbPort={port && !isWifiPort(port) && !isBluetoothPort(port) && !isSimPort(port) ? port : (ports.find((candidate) => !candidate.bluetooth)?.address ?? null)}
				t={t}
				onJoined={(address) => {
					setWifiAddresses(rememberAddress(address));
					setPort(wifiPort(address));
				}}
				onClose={() => setWifiShown(false)}
			/>
			<BluetoothDialog
				opened={bluetoothShown}
				language={language}
				boardId={boardId}
				usbPort={port && !isWifiPort(port) && !isBluetoothPort(port) && !isSimPort(port) ? port : (ports.find((candidate) => !candidate.bluetooth)?.address ?? null)}
				t={t}
				onChosen={(board) => {
					setBluetoothBoards(rememberBoard(board));
					setPort(bluetoothPort(board.address));
				}}
				onClose={() => setBluetoothShown(false)}
			/>
			<ArduinoLibraries
				opened={librariesShown}
				language={language}
				t={t}
				libraries={projectLibraries}
				usesCpp={program === "cpp" || userCode?.language === "cpp"}
				onChange={(next) => {
					setProjectLibraries(next);
					markDirty();
				}}
				onClose={() => setLibrariesShown(false)}
			/>
			<Library
				opened={libraryShown}
				groups={library}
				t={t}
				onInsert={(group) => {
					editor.current?.insert(group.blocks);
					setLibraryShown(false);
				}}
				onRename={(group, groupName) => void renameGroup(group, groupName)}
				onRemove={(group) => void removeGroup(group)}
				onExport={() => void exportLibrary()}
				onImport={() => void importLibrary()}
				onClose={() => setLibraryShown(false)}
			/>
			<ProjectWiring
				opened={wiringShown}
				example={example}
				language={language}
				project={currentProject}
				t={t}
				onShowBlocks={(blockIds) => {
					setWiringShown(false);
					// From the code view, back to the blocks first (they are built again).
					void (view === "blocks" ? Promise.resolve() : showBlocksView()).then(() => editor.current?.highlight(blockIds));
				}}
				onClose={() => setWiringShown(false)}
			/>

			<Modal
				opened={helpShown}
				onClose={() => setHelpShown(false)}
				title={`${t("program.help")} - ${programName(helpLanguage, t("program.blocks"))}`}
				size="lg"
			>
				{helpLanguage === "cpp" ? (
					<Text size="sm">{t("program.help_cpp")}</Text>
				) : (
					<Stack gap="sm">
						<Text size="sm">{t("program.help_script")}</Text>
						<Code block>{t("program.help_functions")}</Code>
						<Text size="sm">{t("program.help_loop")}</Text>
					</Stack>
				)}
			</Modal>
		</AppShell>
	);
}
