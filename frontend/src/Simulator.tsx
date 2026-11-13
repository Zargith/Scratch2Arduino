// Simulation: the virtual board drawn beside the blocks in Live mode on the "sim:" port. Outputs
// (LEDs, screens, servos, sounds...) follow the blocks; buttons and sliders change its inputs.
import { ActionIcon, Button, Card, Group, Slider, Stack, Text, TextInput, Tooltip } from "@mantine/core";
import { IconCpu, IconPropeller, IconSend, IconVolume, IconVolumeOff } from "@tabler/icons-react";
import { useEffect, useRef, useState, type ReactNode } from "react";

import type { SimInput, SimState, WiringPlan } from "@/bridge";
import type { MessageKey } from "@/i18n";
import { outputPins, percent, simParts, type SimPart } from "@/simulation";
import "./Simulator.css";

interface SimulatorProps {
	plan: WiringPlan | null;
	state: SimState | null;
	/** Shows the virtual phone (the program uses the Phone blocks). */
	phone: boolean;
	t(key: MessageKey, params?: Record<string, string>): string;
	onInput(...input: SimInput): void;
}

const ledColor = (level: number) => `rgba(255, 64, 64, ${0.15 + (0.85 * level) / 255})`;

function Led({ level, size = 22, label }: { level: number; size?: number; label: string }) {
	return (
		<div
			role="img"
			aria-label={label}
			className="s2a-sim-led"
			style={{
				width: size,
				height: size,
				background: ledColor(level),
				boxShadow: level > 0 ? `0 0 ${4 + (10 * level) / 255}px ${ledColor(level)}` : undefined,
			}}
		/>
	);
}

/** A slider that answers at once on the screen and sends its value to the virtual board. */
function InputSlider(props: {
	value: number;
	min: number;
	max: number;
	step?: number;
	label: (value: number) => string;
	ariaLabel: string;
	onChange(value: number): void;
}) {
	const [value, setValue] = useState(props.value);
	return (
		<Group gap={6} wrap="nowrap" style={{ flex: 1 }}>
			<Slider
				value={value}
				min={props.min}
				max={props.max}
				step={props.step ?? 1}
				label={null}
				aria-label={props.ariaLabel}
				style={{ flex: 1 }}
				onChange={(next) => {
					setValue(next);
					props.onChange(next);
				}}
			/>
			<Text size="xs" w={48} ta="right">
				{props.label(value)}
			</Text>
		</Group>
	);
}

function Screen({ lines, kind }: { lines: string[]; kind: "lcd" | "oled" }) {
	return (
		<div className={`s2a-sim-screen s2a-sim-${kind}`}>
			{lines.map((line, index) => (
				<div key={index}>{line.padEnd(16, " ")}</div>
			))}
		</div>
	);
}

function Servo({ angle, label }: { angle: number; label: string }) {
	const radians = (angle * Math.PI) / 180;
	const x = 30 - 24 * Math.cos(radians);
	const y = 30 - 24 * Math.sin(radians);
	return (
		<svg width={60} height={34} role="img" aria-label={label}>
			<path d="M 4 30 A 26 26 0 0 1 56 30" fill="none" stroke="var(--mantine-color-gray-5)" strokeWidth={2} />
			<line x1={30} y1={30} x2={x} y2={y} stroke="var(--mantine-color-blue-6)" strokeWidth={4} strokeLinecap="round" />
			<circle cx={30} cy={30} r={4} fill="var(--mantine-color-blue-8)" />
		</svg>
	);
}

/** A stepper motor seen from above: its shaft with a mark that shows the angle turned. */
function Stepper({ angle, label }: { angle: number; label: string }) {
	const radians = ((angle - 90) * Math.PI) / 180;
	return (
		<svg width={34} height={34} role="img" aria-label={label}>
			<circle cx={17} cy={17} r={15} fill="var(--mantine-color-gray-2)" stroke="var(--mantine-color-gray-6)" strokeWidth={2} />
			<line
				x1={17}
				y1={17}
				x2={17 + 12 * Math.cos(radians)}
				y2={17 + 12 * Math.sin(radians)}
				stroke="var(--mantine-color-orange-6)"
				strokeWidth={4}
				strokeLinecap="round"
			/>
			<circle cx={17} cy={17} r={3} fill="var(--mantine-color-gray-8)" />
		</svg>
	);
}

/** The sound of the buzzers, with the browser's audio (a square wave, quiet). */
function useBuzzer(frequency: number, muted: boolean) {
	const audio = useRef<{ context: AudioContext; oscillator: OscillatorNode; gain: GainNode } | null>(null);
	useEffect(() => {
		const sounding = frequency > 0 && !muted;
		if (!sounding) {
			audio.current?.gain.gain.setValueAtTime(0, audio.current.context.currentTime);
			return;
		}
		if (!audio.current) {
			if (typeof AudioContext === "undefined") return;
			const context = new AudioContext();
			const oscillator = context.createOscillator();
			const gain = context.createGain();
			oscillator.type = "square";
			oscillator.connect(gain).connect(context.destination);
			gain.gain.value = 0;
			oscillator.start();
			audio.current = { context, oscillator, gain };
		}
		const { context, oscillator, gain } = audio.current;
		oscillator.frequency.setValueAtTime(frequency, context.currentTime);
		gain.gain.setValueAtTime(0.04, context.currentTime);
	}, [frequency, muted]);
	useEffect(
		() => () => {
			void audio.current?.context.close();
			audio.current = null;
		},
		[]
	);
}

export function Simulator({ plan, state, phone, t, onInput }: SimulatorProps) {
	const [muted, setMuted] = useState(false);
	const [message, setMessage] = useState("");
	const parts = simParts(plan, state);
	const pins = outputPins(state);
	const sounding = parts.find((part) => part.kind === "buzzer" && part.frequency > 0);
	useBuzzer(sounding?.kind === "buzzer" ? sounding.frequency : (Object.values(state?.tones ?? {})[0] ?? 0), muted);

	const pinLabel = (pin: string) => t("sim.pin", { pin });
	const title = (part: SimPart) => (part.detail ? `${part.name}: ${t(part.detail)}` : part.name);
	const widget = (part: SimPart): ReactNode => {
		switch (part.kind) {
			case "led":
				return <Led level={part.level} label={`${part.name}, ${pinLabel(part.pin)}`} />;
			case "rgb": {
				const [red, green, blue] = part.color;
				const lit = Math.max(red, green, blue);
				return (
					<div
						role="img"
						aria-label={part.name}
						className="s2a-sim-led"
						style={{
							background: lit > 0 ? `rgb(${red}, ${green}, ${blue})` : "var(--mantine-color-gray-3)",
							boxShadow: lit > 0 ? `0 0 10px rgb(${red}, ${green}, ${blue})` : undefined,
						}}
					/>
				);
			}
			case "button":
				return (
					<Button
						size="xs"
						variant={part.pressed ? "filled" : "default"}
						aria-pressed={part.pressed}
						onPointerDown={() => onInput("press", part.pin, true)}
						onPointerUp={() => onInput("press", part.pin, false)}
						onPointerLeave={() => part.pressed && onInput("press", part.pin, false)}
						onKeyDown={(event) => {
							if (event.key === " " || event.key === "Enter") onInput("press", part.pin, true);
						}}
						onKeyUp={() => onInput("press", part.pin, false)}
					>
						{t(part.action === "move" ? "sim.move" : part.action === "near" ? "sim.near" : "sim.press")}
					</Button>
				);
			case "slider":
				return (
					<InputSlider
						value={part.value}
						min={0}
						max={1023}
						label={(value) => (part.sensor === "analog" ? String(value) : `${percent(value)} %`)}
						ariaLabel={title(part)}
						onChange={(value) => onInput("analog", part.pin, value)}
					/>
				);
			case "distance":
				return (
					<InputSlider
						value={part.centimetres}
						min={2}
						max={400}
						label={(value) => `${value} cm`}
						ariaLabel={part.name}
						onChange={(value) => onInput("distance", part.pin, value)}
					/>
				);
			case "climate":
				return <Climate part={part} t={t} onInput={onInput} />;
			case "buzzer":
				return (
					<Text size="sm" c={part.frequency > 0 ? "pink" : "dimmed"}>
						{part.frequency > 0 ? `${part.frequency} Hz` : t("sim.silent")}
					</Text>
				);
			case "stepper": {
				const degrees = Math.round(part.angle);
				return (
					<Group gap={6} wrap="nowrap">
						<Stepper angle={part.angle} label={`${part.name}: ${degrees}°`} />
						<Text size="sm">{degrees}°</Text>
					</Group>
				);
			}
			case "servo":
				return (
					<Group gap={6} wrap="nowrap">
						<Servo angle={part.angle} label={`${part.name}: ${part.angle}°`} />
						<Text size="sm">{part.angle}°</Text>
					</Group>
				);
			case "motor": {
				const speed = String(Math.round((Math.abs(part.speed) * 100) / 255));
				const duration = part.speed === 0 ? undefined : `${(0.3 + (1.7 * (255 - Math.abs(part.speed))) / 255).toFixed(2)}s`;
				return (
					<Group gap={6} wrap="nowrap">
						<IconPropeller
							size={22}
							className={part.speed === 0 ? undefined : part.speed > 0 ? "s2a-sim-spin" : "s2a-sim-spin-back"}
							style={duration ? { animationDuration: duration } : undefined}
						/>
						<Text size="sm">
							{part.speed > 0
								? t("sim.forward", { speed })
								: part.speed < 0
									? t("sim.backward", { speed })
									: t("sim.stopped")}
						</Text>
					</Group>
				);
			}
			case "strip":
				return (
					<Group gap={3}>
						{part.colors.map(([red, green, blue], index) => (
							<div
								key={index}
								className="s2a-sim-pixel"
								style={{ background: red + green + blue > 0 ? `rgb(${red}, ${green}, ${blue})` : undefined }}
							/>
						))}
					</Group>
				);
			case "matrix":
				return (
					<div className="s2a-sim-matrix" role="img" aria-label={part.name}>
						{part.rows.flatMap((row, y) =>
							Array.from({ length: 8 }, (_, x) => (
								<div key={`${y}-${x}`} className={(row >> (7 - x)) & 1 ? "on" : undefined} />
							))
						)}
					</div>
				);
			case "screen":
				return <Screen lines={part.lines} kind={part.screen} />;
		}
	};
	/** Screens, strips and matrices are wide: below their name rather than beside it. */
	const below = (part: SimPart) => part.kind === "screen" || part.kind === "matrix" || part.kind === "strip" || part.kind === "climate";

	return (
		<Card withBorder shadow="sm" w={360} style={{ borderColor: "var(--mantine-color-teal-6)" }}>
			<Group justify="space-between" wrap="nowrap" mb={4}>
				<Group gap={6} wrap="nowrap">
					<IconCpu size={18} color="var(--mantine-color-teal-7)" />
					<Text fw={700} size="sm">
						{t("sim.title")}
					</Text>
				</Group>
				<Tooltip label={t(muted ? "sim.unmute" : "sim.mute")}>
					<ActionIcon
						variant="subtle"
						color="gray"
						aria-label={t(muted ? "sim.unmute" : "sim.mute")}
						aria-pressed={muted}
						onClick={() => setMuted((value) => !value)}
					>
						{muted ? <IconVolumeOff size={16} /> : <IconVolume size={16} />}
					</ActionIcon>
				</Tooltip>
			</Group>
			<Text size="xs" c="dimmed" mb="xs">
				{t("sim.help")}
			</Text>
			<Stack gap="xs" style={{ maxHeight: "calc(60dvh - 90px)", overflowY: "auto" }}>
				{parts.length === 0 && pins.length === 0 && (
					<Text size="sm" c="dimmed">
						{t("sim.empty")}
					</Text>
				)}
				{parts.map((part) =>
					below(part) ? (
						<Stack key={part.key} gap={4}>
							<Text size="sm">{title(part)}</Text>
							{widget(part)}
						</Stack>
					) : (
						<Group key={part.key} justify="space-between" gap="sm" wrap="nowrap">
							<Text size="sm" truncate style={{ flex: part.kind === "slider" || part.kind === "distance" ? "0 0 45%" : 1 }}>
								{title(part)}
							</Text>
							{widget(part)}
						</Group>
					)
				)}
				{pins.length > 0 && (
					<Stack gap={4}>
						<Text size="xs" c="dimmed">
							{t("sim.pins")}
						</Text>
						<Group gap="xs">
							{pins.map(({ pin, level }) => (
								<Stack key={pin} gap={0} align="center">
									<Led level={level} size={12} label={pinLabel(pin)} />
									<Text size="xs">{pin}</Text>
								</Stack>
							))}
						</Group>
					</Stack>
				)}
				{phone && (
					<Stack gap={4}>
						<Text size="sm">{t("sim.phone")}</Text>
						{(state?.phone ?? []).slice(-3).map((line, index) => (
							<Text key={index} size="sm" className="s2a-sim-bubble">
								{line}
							</Text>
						))}
						<form
							onSubmit={(event) => {
								event.preventDefault();
								if (!message.trim()) return;
								onInput("phone", message);
								setMessage("");
							}}
						>
							<Group gap="xs" wrap="nowrap">
								<TextInput
									size="xs"
									style={{ flex: 1 }}
									placeholder={t("sim.phone_placeholder")}
									aria-label={t("sim.phone_placeholder")}
									value={message}
									onChange={(event) => setMessage(event.currentTarget.value)}
								/>
								<ActionIcon type="submit" variant="light" aria-label={t("sim.send")}>
									<IconSend size={16} />
								</ActionIcon>
							</Group>
						</form>
					</Stack>
				)}
			</Stack>
		</Card>
	);
}

function Climate({
	part,
	t,
	onInput,
}: {
	part: Extract<SimPart, { kind: "climate" }>;
	t: SimulatorProps["t"];
	onInput: SimulatorProps["onInput"];
}) {
	const values = useRef({ temperature: part.temperature, humidity: part.humidity });
	const send = () => onInput("climate", part.pin, values.current.temperature, values.current.humidity);
	return (
		<Stack gap={4}>
			<Group gap="sm" wrap="nowrap">
				<Text size="xs" w={90}>
					{t("sim.temperature")}
				</Text>
				<InputSlider
					value={part.temperature}
					min={-10}
					max={50}
					step={0.5}
					label={(value) => `${value} °C`}
					ariaLabel={t("sim.temperature")}
					onChange={(value) => {
						values.current.temperature = value;
						send();
					}}
				/>
			</Group>
			<Group gap="sm" wrap="nowrap">
				<Text size="xs" w={90}>
					{t("sim.humidity")}
				</Text>
				<InputSlider
					value={part.humidity}
					min={0}
					max={100}
					label={(value) => `${value} %`}
					ariaLabel={t("sim.humidity")}
					onChange={(value) => {
						values.current.humidity = value;
						send();
					}}
				/>
			</Group>
		</Stack>
	);
}
