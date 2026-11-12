// Library manager: the Arduino libraries of the project (installed by the Build when missing),
// a search in the Arduino registry, and the libraries installed on this computer.
import {
	ActionIcon,
	Alert,
	Badge,
	Button,
	Card,
	Code,
	Group,
	Loader,
	Modal,
	ScrollArea,
	Select,
	Stack,
	Tabs,
	Text,
	TextInput,
	Tooltip,
} from "@mantine/core";
import { IconBooks, IconPlus, IconRefresh, IconSearch, IconTrash, IconX } from "@tabler/icons-react";
import { useCallback, useEffect, useState } from "react";

import { bridge, hasHost, type InstalledLibrary, type RegistryLibrary } from "@/bridge";
import type { Language } from "@/catalog";
import type { MessageKey } from "@/i18n";
import type { ProjectLibrary } from "@/project/convert";

interface ArduinoLibrariesProps {
	opened: boolean;
	language: Language;
	t(key: MessageKey, params?: Record<string, string>): string;
	/** The libraries of the project. */
	libraries: ProjectLibrary[];
	/** The project has C++ code (a "My code" file in C++, or a C++ program): libraries serve it. */
	usesCpp: boolean;
	onChange(libraries: ProjectLibrary[]): void;
	onClose(): void;
}

type Tab = "project" | "search" | "installed";

interface Message {
	ok: boolean;
	text: string;
	details?: string;
}

/** The project's list with `library` added or replaced, sorted by name. */
export function addLibrary(libraries: ProjectLibrary[], library: ProjectLibrary): ProjectLibrary[] {
	const others = libraries.filter((candidate) => candidate.name !== library.name);
	return [...others, library].sort((a, b) => a.name.localeCompare(b.name));
}

/** "#include <X.h>" lines for a library (its first headers), empty when unknown. */
export function includeLines(library: InstalledLibrary | undefined): string {
	return (library?.includes ?? [])
		.filter((header) => /\.h$/.test(header))
		.slice(0, 3)
		.map((header) => `#include <${header}>`)
		.join("\n");
}

function errorText(error: unknown): string {
	return error instanceof Error ? error.message : String(error);
}

export function ArduinoLibraries({ opened, language, t, libraries, usesCpp, onChange, onClose }: ArduinoLibrariesProps) {
	const [tab, setTab] = useState<Tab>("project");
	const [installed, setInstalled] = useState<InstalledLibrary[]>([]);
	const [query, setQuery] = useState("");
	const [results, setResults] = useState<RegistryLibrary[] | null>(null);
	const [searching, setSearching] = useState(false);
	/** Version picked for each search result (the latest by default). */
	const [versions, setVersions] = useState<Record<string, string>>({});
	/** Name of the library being installed or removed. */
	const [working, setWorking] = useState<string | null>(null);
	const [message, setMessage] = useState<Message | null>(null);

	const refreshInstalled = useCallback(async () => {
		if (!hasHost()) return;
		try {
			setInstalled(await bridge.librariesInstalled(language));
		} catch (error) {
			setMessage({ ok: false, text: errorText(error) });
		}
	}, [language]);

	useEffect(() => {
		if (!opened) return;
		setMessage(null);
		setTab(libraries.length > 0 ? "project" : "search");
		void refreshInstalled();
		// Only when the window opens.
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, [opened]);

	const installedNamed = (name: string) => installed.find((library) => library.name === name);
	const inProject = (name: string) => libraries.some((library) => library.name === name);

	const search = async () => {
		if (!hasHost()) {
			setMessage({ ok: false, text: t("status.no_host") });
			return;
		}
		setSearching(true);
		setMessage(null);
		try {
			setResults(await bridge.librariesSearch(query.trim(), language));
		} catch (error) {
			setResults(null);
			setMessage({ ok: false, text: errorText(error) });
		} finally {
			setSearching(false);
		}
	};

	/** Installs `name` (empty version: the latest), then runs `then` when it worked. */
	const install = async (name: string, version: string, done: MessageKey, then?: () => void) => {
		if (!hasHost()) {
			setMessage({ ok: false, text: t("status.no_host") });
			return;
		}
		setWorking(name);
		setMessage({ ok: true, text: t("libraries.installing", { name }) });
		try {
			const change = await bridge.libraryInstall(name, version, language);
			if (change.ok) {
				then?.();
				setMessage({ ok: true, text: t(done, { name }) });
			} else {
				setMessage({ ok: false, text: t("libraries.failed", { name, message: change.message }), details: change.details });
			}
		} catch (error) {
			setMessage({ ok: false, text: t("libraries.failed", { name, message: errorText(error) }) });
		} finally {
			setWorking(null);
			await refreshInstalled();
		}
	};

	const addToProject = (name: string, version: string) =>
		void install(name, version, "libraries.added", () => onChange(addLibrary(libraries, version ? { name, version } : { name })));

	const uninstall = async (library: InstalledLibrary) => {
		if (!window.confirm(t("libraries.confirm_uninstall", { name: library.name }))) return;
		setWorking(library.name);
		setMessage(null);
		try {
			const change = await bridge.libraryUninstall(library.name, language);
			setMessage(
				change.ok
					? { ok: true, text: t("libraries.uninstalled", { name: library.name }) }
					: { ok: false, text: t("libraries.failed", { name: library.name, message: change.message }), details: change.details }
			);
		} catch (error) {
			setMessage({ ok: false, text: t("libraries.failed", { name: library.name, message: errorText(error) }) });
		} finally {
			setWorking(null);
			await refreshInstalled();
		}
	};

	const busy = working !== null;

	const projectTab = (
		<Stack gap="xs">
			{libraries.length === 0 ? (
				<Text size="sm" c="dimmed">
					{t("libraries.project_empty")}
				</Text>
			) : (
				libraries.map((library) => {
					const local = installedNamed(library.name);
					const include = includeLines(local);
					return (
						<Card key={library.name} withBorder padding="sm">
							<Group justify="space-between" wrap="nowrap" align="flex-start">
								<Stack gap={4} style={{ minWidth: 0 }}>
									<Group gap="xs">
										<Text fw={600}>{library.name}</Text>
										<Badge variant="light" color="gray">
											{library.version ?? t("libraries.any_version")}
										</Badge>
										{local ? (
											<Badge variant="light" color="green">
												{t("libraries.installed_version", { version: local.version })}
											</Badge>
										) : (
											<Badge variant="light" color="orange">
												{t("libraries.not_installed")}
											</Badge>
										)}
									</Group>
									{local?.sentence && (
										<Text size="xs" c="dimmed">
											{local.sentence}
										</Text>
									)}
									{include && (
										<>
											<Text size="xs">{t("libraries.include")}</Text>
											<Code block>{include}</Code>
										</>
									)}
								</Stack>
								<Tooltip label={t("libraries.remove_from_project")}>
									<ActionIcon
										variant="subtle"
										color="red"
										aria-label={t("libraries.remove_from_project")}
										disabled={busy}
										onClick={() => onChange(libraries.filter((candidate) => candidate.name !== library.name))}
									>
										<IconX size={16} />
									</ActionIcon>
								</Tooltip>
							</Group>
						</Card>
					);
				})
			)}
		</Stack>
	);

	const searchTab = (
		<Stack gap="xs">
			<Group gap="xs" wrap="nowrap">
				<TextInput
					aria-label={t("libraries.search")}
					placeholder={t("libraries.search_placeholder")}
					value={query}
					onChange={(event) => setQuery(event.currentTarget.value)}
					onKeyDown={(event) => {
						if (event.key === "Enter") void search();
					}}
					style={{ flex: 1 }}
					disabled={searching}
					data-autofocus
				/>
				<Button leftSection={<IconSearch size={16} />} onClick={() => void search()} loading={searching}>
					{t("libraries.search")}
				</Button>
			</Group>
			<Text size="xs" c="dimmed">
				{t("libraries.english_note")}
			</Text>
			{searching && (
				<Group gap="xs">
					<Loader size="xs" />
					<Text size="sm">{t("libraries.searching")}</Text>
				</Group>
			)}
			{!searching && results?.length === 0 && (
				<Text size="sm" c="dimmed">
					{t("libraries.no_result")}
				</Text>
			)}
			{!searching &&
				results?.map((library) => {
					const local = installedNamed(library.name);
					const chosen = versions[library.name] ?? library.latestVersion;
					const newestFirst = [...library.versions].reverse();
					return (
						<Card key={library.name} withBorder padding="sm">
							<Stack gap={4}>
								<Group gap="xs">
									<Text fw={600}>{library.name}</Text>
									{library.author && (
										<Text size="xs" c="dimmed">
											{library.author}
										</Text>
									)}
									{local && (
										<Badge variant="light" color="green">
											{t("libraries.installed_version", { version: local.version })}
										</Badge>
									)}
								</Group>
								{library.sentence && <Text size="sm">{library.sentence}</Text>}
								<Group gap="xs" justify="flex-end">
									{newestFirst.length > 1 && (
										<Select
											aria-label={t("libraries.version")}
											size="xs"
											w={170}
											data={newestFirst.map((version) => ({
												value: version,
												label: version === library.latestVersion ? t("libraries.latest", { version }) : version,
											}))}
											value={chosen}
											onChange={(value) => value && setVersions({ ...versions, [library.name]: value })}
											allowDeselect={false}
											disabled={busy}
										/>
									)}
									{inProject(library.name) ? (
										<Badge variant="filled" color="blue">
											{t("libraries.in_project")}
										</Badge>
									) : (
										<Button
											size="xs"
											leftSection={<IconPlus size={14} />}
											loading={working === library.name}
											disabled={busy && working !== library.name}
											onClick={() => addToProject(library.name, chosen === library.latestVersion ? "" : chosen)}
										>
											{t("libraries.add")}
										</Button>
									)}
								</Group>
							</Stack>
						</Card>
					);
				})}
		</Stack>
	);

	const installedTab = (
		<Stack gap="xs">
			{installed.length === 0 && (
				<Text size="sm" c="dimmed">
					{t("libraries.none_installed")}
				</Text>
			)}
			{installed.map((library) => (
				<Card key={library.name} withBorder padding="sm">
					<Group justify="space-between" wrap="nowrap" align="flex-start">
						<Stack gap={4} style={{ minWidth: 0 }}>
							<Group gap="xs">
								<Text fw={600}>{library.name}</Text>
								<Badge variant="light" color="gray">
									{library.version}
								</Badge>
								{library.builtIn && (
									<Badge variant="light" color="grape">
										{t("libraries.built_in")}
									</Badge>
								)}
								{inProject(library.name) && (
									<Badge variant="light" color="blue">
										{t("libraries.in_project")}
									</Badge>
								)}
							</Group>
							{library.sentence && (
								<Text size="xs" c="dimmed">
									{library.sentence}
								</Text>
							)}
						</Stack>
						<Group gap={4} wrap="nowrap">
							{!inProject(library.name) && (
								<Tooltip label={t("libraries.add")}>
									<ActionIcon
										variant="subtle"
										aria-label={t("libraries.add")}
										disabled={busy}
										onClick={() => onChange(addLibrary(libraries, { name: library.name }))}
									>
										<IconPlus size={16} />
									</ActionIcon>
								</Tooltip>
							)}
							{!library.builtIn && (
								<>
									<Tooltip label={t("libraries.update")}>
										<ActionIcon
											variant="subtle"
											aria-label={t("libraries.update")}
											loading={working === library.name}
											disabled={busy}
											onClick={() => void install(library.name, "", "libraries.updated")}
										>
											<IconRefresh size={16} />
										</ActionIcon>
									</Tooltip>
									<Tooltip label={t("libraries.uninstall")}>
										<ActionIcon
											variant="subtle"
											color="red"
											aria-label={t("libraries.uninstall")}
											disabled={busy}
											onClick={() => void uninstall(library)}
										>
											<IconTrash size={16} />
										</ActionIcon>
									</Tooltip>
								</>
							)}
						</Group>
					</Group>
				</Card>
			))}
		</Stack>
	);

	return (
		<Modal
			opened={opened}
			onClose={onClose}
			title={
				<Group gap="xs">
					<IconBooks size={20} />
					<Text fw={700}>{t("libraries.title")}</Text>
				</Group>
			}
			size="xl"
		>
			<Stack gap="sm">
				<Text size="sm" c="dimmed">
					{t("libraries.help")}
				</Text>
				{!usesCpp && (
					<Alert color="yellow" variant="light">
						{t("libraries.not_cpp")}
					</Alert>
				)}
				<Tabs value={tab} onChange={(value) => value && setTab(value as Tab)}>
					<Tabs.List>
						<Tabs.Tab value="project">{t("libraries.tab_project", { count: String(libraries.length) })}</Tabs.Tab>
						<Tabs.Tab value="search">{t("libraries.tab_search")}</Tabs.Tab>
						<Tabs.Tab value="installed">{t("libraries.tab_installed")}</Tabs.Tab>
					</Tabs.List>
				</Tabs>
				<ScrollArea.Autosize mah="55vh">
					{tab === "project" ? projectTab : tab === "search" ? searchTab : installedTab}
				</ScrollArea.Autosize>
				{message && (
					<Alert color={message.ok ? "green" : "red"} variant="light" withCloseButton onClose={() => setMessage(null)}>
						<Text size="sm">{message.text}</Text>
						{message.details && (
							<Code block mt="xs" style={{ whiteSpace: "pre-wrap", maxHeight: 120, overflow: "auto" }}>
								{message.details}
							</Code>
						)}
					</Alert>
				)}
			</Stack>
		</Modal>
	);
}
