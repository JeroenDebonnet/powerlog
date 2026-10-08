import pandas as pd
import plotly.graph_objects as go


VALUE_COLORS = {
    "l1_consumption_kw": "red",
    "l2_consumption_kw": "green",
    "l3_consumption_kw": "blue",
    "l3_injection_kw": "orange",
    "t1_consumption_meter_kwh": "red",
    "t2_consumption_meter_kwh": "green",
    "t1_injection_meter_kwh": "blue",
    "t2_injection_meter_kwh": "orange",
    "gas_meter_m3": "#172554",
}
VALUE_RENDER_MODES = {
    "l1_consumption_kw": "line",
    "l2_consumption_kw": "line",
    "l3_consumption_kw": "line",
    "l3_injection_kw": "area",
    "t1_consumption_meter_kwh": "line",
    "t2_consumption_meter_kwh": "line",
    "t1_injection_meter_kwh": "line",
    "t2_injection_meter_kwh": "line",
    "gas_meter_m3": "line",
}


def build_power_graph(df, filename, time_column, selected_columns):
    """Build a time-series figure from the selected power-log columns.

    Args:
        df: DataFrame containing the CSV rows.
        filename: Name used as the figure title and UI revision key.
        time_column: DataFrame column containing timestamps.
        selected_columns: Numeric columns to include in the figure.

    Returns:
        A Plotly figure with area traces behind line traces.
    """
    df[time_column] = pd.to_datetime(df[time_column], errors="coerce")
    df = df.dropna(subset=[time_column]).sort_values(time_column)

    figure = go.Figure()
    draw_order = sorted(
        selected_columns,
        key=lambda column: VALUE_RENDER_MODES.get(column) != "area",
    )
    legend_ranks = {column: rank for rank, column in enumerate(selected_columns)}

    for column in draw_order:
        values = pd.to_numeric(df[column], errors="coerce")
        figure.add_trace(
            go.Scattergl(
                x=df[time_column],
                y=values,
                mode="lines",
                name=column,
                legendrank=legend_ranks[column],
                line={"color": VALUE_COLORS.get(column)},
                fill=(
                    "tozeroy"
                    if VALUE_RENDER_MODES.get(column) == "area"
                    else None
                ),
                connectgaps=False,
            )
        )

    figure.update_layout(
        title=filename,
        xaxis_title="Tijd",
        yaxis_title="Waarde",
        hovermode="x unified",
        legend_title="Meetwaarden",
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
