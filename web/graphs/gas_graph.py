import pandas as pd
import plotly.graph_objects as go


def build_gas_graph(df, filename):
	"""Build a gas-consumption graph from consecutive meter readings.

	Args:
		df: DataFrame containing timestamps and gas meter readings in m3.
		filename: Name used as the figure title and UI revision key.

	Returns:
		A Plotly figure containing gas consumption deltas, not meter readings.
	"""
	df = df.copy()
	df["timestamp"] = pd.to_datetime(df["timestamp"], errors="coerce")
	df["gas_meter_m3"] = pd.to_numeric(df["gas_meter_m3"], errors="coerce")
	df = df.dropna(subset=["timestamp"]).sort_values("timestamp")

	consumption = df["gas_meter_m3"].diff()
	consumption = consumption.where(consumption >= 0)

	figure = go.Figure(
		go.Scattergl(
			x=df["timestamp"],
			y=consumption,
			mode="lines",
			name="Gasverbruik",
			line={"color": "#172554"},
			connectgaps=False,
		)
	)
	figure.update_layout(
		title=filename,
		xaxis_title="Tijd",
		yaxis_title="Gasverbruik per meetinterval (m³)",
		hovermode="x unified",
		margin={"l": 60, "r": 30, "t": 60, "b": 60},
		uirevision=filename,
	)
	figure.update_xaxes(
		type="date",
		rangeslider={"visible": True},
		rangeselector={
			"buttons": [
				{"count": 1, "label": "1 min", "step": "minute", "stepmode": "backward"},
				{"count": 10, "label": "10 min", "step": "minute", "stepmode": "backward"},
				{"count": 1, "label": "1 uur", "step": "hour", "stepmode": "backward"},
				{"count": 6, "label": "6 uur", "step": "hour", "stepmode": "backward"},
				{"count": 1, "label": "1 dag", "step": "day", "stepmode": "backward"},
				{"label": "Alles", "step": "all"},
			]
		},
	)
	return figure
