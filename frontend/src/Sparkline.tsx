// Small line chart of the last values of a variable or a sensor, for the Live panel.
interface SparklineProps {
	values: number[];
	width?: number;
	height?: number;
	label: string;
}

export function Sparkline({ values, width = 120, height = 32, label }: SparklineProps) {
	if (values.length < 2) return <svg width={width} height={height} aria-hidden />;
	const min = Math.min(...values);
	const max = Math.max(...values);
	const span = max - min || 1;
	const step = width / (values.length - 1);
	// A flat line sits in the middle.
	const y = (value: number) => (max === min ? height / 2 : 2 + (height - 4) * (1 - (value - min) / span));
	const points = values.map((value, index) => `${(index * step).toFixed(1)},${y(value).toFixed(1)}`).join(" ");
	return (
		<svg width={width} height={height} role="img" aria-label={label} style={{ display: "block", flexShrink: 0 }}>
			<polyline
				points={points}
				fill="none"
				stroke="var(--mantine-primary-color-filled)"
				strokeWidth={2}
				strokeLinejoin="round"
				strokeLinecap="round"
			/>
		</svg>
	);
}
