from pathlib import Path

import pandas as pd

from dash import Dash, Input, Output, State, dcc, html
from dash.exceptions import PreventUpdate
from graphs.gas_graph import build_gas_graph
from graphs.netto_power_graph import build_netto_power_graph
from graphs.power_graph import build_power_graph


CSV_DIRECTORY = Path("../build/logs").resolve()
VALUE_PRESETS = {
    "power": (
        "l1_consumption_kw",
        "l2_consumption_kw",
        "l3_consumption_kw",
        "l3_injection_kw",
    ),
    "meters": (
        "t1_consumption_meter_kwh",
        "t2_consumption_meter_kwh",
        "t1_injection_meter_kwh",
        "t2_injection_meter_kwh",
        "gas_meter_m3",
    ),
}
app = Dash(__name__)
app.title = "PowerLog CSV Viewer"


def list_csv_files():
    return sorted(
        CSV_DIRECTORY.glob("*.csv"),
        key=lambda path: path.stat().st_mtime,
        reverse=True
    )


def safe_csv_path(filename):
    if not filename:
        raise ValueError("Geen bestandsnaam opgegeven")

    path = (CSV_DIRECTORY / filename).resolve()

    # Vermijdt toegang buiten CSV_DIRECTORY.
    if path.parent != CSV_DIRECTORY:
        raise ValueError("Ongeldig bestandspad")

    if path.suffix.lower() != ".csv" or not path.is_file():
        raise ValueError("CSV-bestand bestaat niet")

    return path


def read_csv(filename):
    path = safe_csv_path(filename)

    # Pas eventueel sep=";" toe als jouw CSV puntkomma's gebruikt.
    return pd.read_csv(path)


def daily_energy_totals(df):
    totals = []
    for columns in (
        ("t1_consumption_meter_kwh", "t2_consumption_meter_kwh"),
        ("t1_injection_meter_kwh", "t2_injection_meter_kwh"),
    ):
        if not all(column in df.columns for column in columns):
            totals.append(None)
            continue

        deltas = []
        for column in columns:
            readings = pd.to_numeric(df[column], errors="coerce").dropna()
            if len(readings) < 2:
                deltas = []
                break
            deltas.append(readings.iloc[-1] - readings.iloc[0])

        totals.append(sum(deltas) if deltas else None)

    return totals


def integrated_power_totals(df):
    consumption_columns = (
        "l1_consumption_kw",
        "l2_consumption_kw",
        "l3_consumption_kw",
    )
    injection_columns = (
        "l1_injection_kw",
        "l2_injection_kw",
        "l3_injection_kw",
    )
    power_columns = consumption_columns + injection_columns
    if "timestamp" not in df.columns or not all(
        column in df.columns for column in power_columns
    ):
        return None, None

    samples = df[["timestamp", *power_columns]].copy()
    samples["timestamp"] = pd.to_datetime(samples["timestamp"], errors="coerce")
    samples = samples.dropna(subset=["timestamp"]).sort_values("timestamp")
    elapsed_seconds = samples["timestamp"].diff().dt.total_seconds()
    valid_interval = elapsed_seconds.gt(0) & elapsed_seconds.le(5)

    def integrate(columns):
        total = 0
        for column in columns:
            power = pd.to_numeric(samples[column], errors="coerce")
            previous_power = power.shift(1)
            interval_energy = (
                (power + previous_power)
                * 0.5
                * elapsed_seconds
                / 3600
            )
            valid_power = power.ge(0) & previous_power.ge(0)
            total += interval_energy.where(valid_interval & valid_power).sum()
        return total

    return integrate(consumption_columns), integrate(injection_columns)


def format_energy_total(value):
    if value is None:
        return "niet beschikbaar"
    return f"{value:.2f}".replace(".", ",")


app.layout = html.Div(
    style={
        "fontFamily": "Arial, sans-serif",
        "maxWidth": "1600px",
        "margin": "0 auto",
        "padding": "20px",
    },
    children=[
        html.H1("PowerLog CSV Viewer"),

        html.Div(
            style={
                "display": "grid",
                "gridTemplateColumns": "1fr 1fr 2fr auto",
                "gap": "15px",
                "alignItems": "end",
            },
            children=[
                html.Div([
                    html.Label("CSV-bestand"),
                    dcc.Dropdown(
                        id="csv-file",
                        options=[
                            {"label": path.name, "value": path.name}
                            for path in list_csv_files()
                        ],
                        value=(
                            list_csv_files()[0].name
                            if list_csv_files()
                            else None
                        ),
                        clearable=False,
                    ),
                ]),

                html.Div([
                    html.Label("Grafiektype"),
                    dcc.RadioItems(
                        id="graph-type",
                        options=[
                            {"label": "Vermogen", "value": "power_graph"},
                            {"label": "Netto vermogen", "value": "netto_power_graph"},
                            {"label": "Gas", "value": "gas_graph"},
                        ],
                        value="power_graph",
                        inline=True,
                        labelStyle={"display": "inline-block", "marginRight": "12px"},
                    ),
                ]),

                html.Div([
                    html.Label("Waarden in grafiek"),
                    dcc.RadioItems(
                        id="value-preset",
                        options=[
                            {"label": "Vermogen", "value": "power"},
                            {"label": "Meterstanden", "value": "meters"},
                        ],
                        value="power",
                        inline=True,
                        labelStyle={"display": "inline-block", "marginRight": "12px"},
                    ),
                    dcc.Dropdown(
                        id="value-columns",
                        multi=True,
                    ),
                ], id="power-value-controls"),

                html.Button(
                    "Bestanden vernieuwen",
                    id="refresh-files",
                    n_clicks=0,
                    style={
                        "height": "38px",
                        "padding": "0 20px",
                    },
                ),
            ],
        ),

        html.Div(
            id="status",
            style={
                "marginTop": "15px",
                "color": "#555",
            },
        ),

        dcc.Loading(
            children=[
                dcc.Graph(
                    id="time-graph",
                    style={"height": "75vh"},
                    config={
                        "displaylogo": False,
                        "scrollZoom": True,
                    },
                ),
                html.Div(
                    id="daily-energy-summary",
                    style={"marginTop": "8px", "fontSize": "16px"},
                ),
            ],
        ),
    ],
)


@app.callback(
    Output("csv-file", "options"),
    Input("refresh-files", "n_clicks"),
)
def refresh_file_list(_):
    return [
        {"label": path.name, "value": path.name}
        for path in list_csv_files()
    ]


@app.callback(
    Output("value-columns", "options"),
    Output("value-columns", "value"),
    Output("value-preset", "value"),
    Output("status", "children"),
    Input("csv-file", "value"),
)
def load_columns(filename):
    if not filename:
        return [], [], "power", "Geen CSV-bestand beschikbaar."

    try:
        df = read_csv(filename)
    except Exception as exc:
        return [], [], "power", f"Fout bij lezen van CSV: {exc}"

    columns = list(df.columns)

    if not columns:
        return [], [], "power", "Het CSV-bestand bevat geen kolommen."

    if "timestamp" not in columns:
        return [], [], "power", f"CSV-bestand {filename} bevat geen timestamp-kolom."

    numeric_columns = [
        column
        for column in columns
        if column != "timestamp"
        and pd.api.types.is_numeric_dtype(
            pd.to_numeric(df[column], errors="coerce")
        )
    ]

    default_values = [
        column for column in VALUE_PRESETS["power"] if column in numeric_columns
    ]

    options = [
        {"label": column, "value": column}
        for column in columns
    ]

    value_options = [
        {"label": column, "value": column}
        for column in numeric_columns
    ]

    return (
        value_options,
        default_values,
        "power",
        f"{len(df):,} records geladen uit {filename}".replace(",", "."),
    )


@app.callback(
    Output("power-value-controls", "style"),
    Input("graph-type", "value"),
)
def toggle_power_controls(graph_type):
    return {"display": "none"} if graph_type != "power_graph" else {}


@app.callback(
    Output("value-columns", "value", allow_duplicate=True),
    Input("value-preset", "value"),
    State("value-columns", "options"),
    prevent_initial_call=True,
)
def select_value_preset(preset, options):
    available_columns = {option["value"] for option in options or []}
    return [
        column for column in VALUE_PRESETS[preset] if column in available_columns
    ]


@app.callback(
    Output("time-graph", "figure"),
    Output("daily-energy-summary", "children"),
    Input("csv-file", "value"),
    Input("value-columns", "value"),
    Input("graph-type", "value"),
)
def update_graph(filename, value_columns, graph_type):
    if not filename:
        raise PreventUpdate

    df = read_csv(filename)

    if "timestamp" not in df.columns:
        raise PreventUpdate

    if graph_type == "gas_graph":
        if "gas_meter_m3" not in df.columns:
            raise PreventUpdate
        return build_gas_graph(df, filename), []

    if graph_type == "netto_power_graph":
        return build_netto_power_graph(df, filename), []

    if not value_columns:
        raise PreventUpdate

    selected_columns = [
        column
        for column in value_columns
        if column in df.columns
    ]

    if not selected_columns:
        raise PreventUpdate

    consumption, injection = daily_energy_totals(df)
    integrated_consumption, integrated_injection = integrated_power_totals(df)
    summary = [
        html.Div(
            f"Verbruikt: meterstanden {format_energy_total(consumption)} kWh | "
            f"vermogensintegratie {format_energy_total(integrated_consumption)} kWh"
        ),
        html.Div(
            f"Geïnjecteerd: meterstanden {format_energy_total(injection)} kWh | "
            f"vermogensintegratie {format_energy_total(integrated_injection)} kWh"
        ),
    ]
    return (
        build_power_graph(df, filename, "timestamp", selected_columns),
        summary,
    )


if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=8050,
        debug=False,
    )
