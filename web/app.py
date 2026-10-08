from pathlib import Path

import pandas as pd
import plotly.graph_objects as go

from dash import Dash, Input, Output, State, dcc, html
from dash.exceptions import PreventUpdate


CSV_DIRECTORY = Path("../build/logs").resolve()

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
                    html.Label("Tijdskolom"),
                    dcc.Dropdown(
                        id="time-column",
                        clearable=False,
                    ),
                ]),

                html.Div([
                    html.Label("Waarden in grafiek"),
                    dcc.Dropdown(
                        id="value-columns",
                        multi=True,
                    ),
                ]),

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
    Output("time-column", "options"),
    Output("time-column", "value"),
    Output("value-columns", "options"),
    Output("value-columns", "value"),
    Output("status", "children"),
    Input("csv-file", "value"),
)
def load_columns(filename):
    if not filename:
        return [], None, [], [], "Geen CSV-bestand beschikbaar."

    try:
        df = read_csv(filename)
    except Exception as exc:
        return [], None, [], [], f"Fout bij lezen van CSV: {exc}"

    columns = list(df.columns)

    if not columns:
        return [], None, [], [], "Het CSV-bestand bevat geen kolommen."

    # Zoek een waarschijnlijke tijdskolom.
    time_names = {
        "time",
        "timestamp",
        "datetime",
        "date",
        "tijd",
        "datum",
    }

    time_column = next(
        (
            column
            for column in columns
            if column.strip().lower() in time_names
        ),
        columns[0],
    )

    numeric_columns = [
        column
        for column in columns
        if column != time_column
        and pd.api.types.is_numeric_dtype(
            pd.to_numeric(df[column], errors="coerce")
        )
    ]

    # Toon standaard maximaal de eerste drie meetwaarden.
    default_values = numeric_columns[:3]

    options = [
        {"label": column, "value": column}
        for column in columns
    ]

    value_options = [
        {"label": column, "value": column}
        for column in numeric_columns
    ]

    return (
        options,
        time_column,
        value_options,
        default_values,
        f"{len(df):,} records geladen uit {filename}".replace(",", "."),
    )


@app.callback(
    Output("time-graph", "figure"),
    Input("csv-file", "value"),
    Input("time-column", "value"),
    Input("value-columns", "value"),
)
def update_graph(filename, time_column, value_columns):
    if not filename or not time_column or not value_columns:
        raise PreventUpdate

    df = read_csv(filename)

    if time_column not in df.columns:
        raise PreventUpdate

    selected_columns = [
        column
        for column in value_columns
        if column in df.columns
    ]

    if not selected_columns:
        raise PreventUpdate

    df[time_column] = pd.to_datetime(
        df[time_column],
        errors="coerce",
    )

    df = df.dropna(subset=[time_column])
    df = df.sort_values(time_column)

    figure = go.Figure()

    for column in selected_columns:
        values = pd.to_numeric(df[column], errors="coerce")

        figure.add_trace(
            go.Scattergl(
                x=df[time_column],
                y=values,
                mode="lines",
                name=column,
                connectgaps=False,
            )
        )

    figure.update_layout(
        title=filename,
        xaxis_title="Tijd",
        yaxis_title="Waarde",
        hovermode="x unified",
        legend_title="Meetwaarden",
        margin={
            "l": 60,
            "r": 30,
            "t": 60,
            "b": 60,
        },
        uirevision=filename,
    )

    figure.update_xaxes(
        type="date",
        rangeslider={"visible": True},
        rangeselector={
            "buttons": [
                {
                    "count": 1,
                    "label": "1 min",
                    "step": "minute",
                    "stepmode": "backward",
                },
                {
                    "count": 10,
                    "label": "10 min",
                    "step": "minute",
                    "stepmode": "backward",
                },
                {
                    "count": 1,
                    "label": "1 uur",
                    "step": "hour",
                    "stepmode": "backward",
                },
                {
                    "count": 6,
                    "label": "6 uur",
                    "step": "hour",
                    "stepmode": "backward",
                },
                {
                    "count": 1,
                    "label": "1 dag",
                    "step": "day",
                    "stepmode": "backward",
                },
                {
                    "label": "Alles",
                    "step": "all",
                },
            ]
        },
    )

    return figure


if __name__ == "__main__":
    app.run(
        host="0.0.0.0",
        port=8050,
        debug=False,
    )
