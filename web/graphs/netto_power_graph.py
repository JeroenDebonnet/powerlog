import pandas as pd
import plotly.graph_objects as go


CONSUMPTION_COLUMNS = (
    "l1_consumption_kw",
    "l2_consumption_kw",
    "l3_consumption_kw",
)
INJECTION_COLUMNS = (
    "l1_injection_kw",
    "l2_injection_kw",
    "l3_injection_kw",
)


def build_netto_power_graph(df, filename):
    """Build a time-series graph of total consumption minus total injection.

    Args:
        df: DataFrame containing timestamps and all six phase power columns.
        filename: Name used as the figure title and UI revision key.

    Returns:
        A Plotly figure containing net power in kW.
    """
    df = df.copy()
    df["timestamp"] = pd.to_datetime(df["timestamp"], errors="coerce")
    df = df.dropna(subset=["timestamp"]).sort_values("timestamp")

    consumption = sum(
        pd.to_numeric(df[column], errors="coerce")
        for column in CONSUMPTION_COLUMNS
    )
    injection = sum(
        pd.to_numeric(df[column], errors="coerce")
        for column in INJECTION_COLUMNS
    )
    net_power = consumption - injection

    figure = go.Figure(
        go.Scattergl(
            x=df["timestamp"],
            y=net_power,
            mode="lines",
            name="Netto vermogen",
            line={"color": "#172554"},
            connectgaps=False,
        )
    )
    figure.update_layout(
        title=filename,
        xaxis_title="Tijd",
        yaxis_title="Netto vermogen (kW)",
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
