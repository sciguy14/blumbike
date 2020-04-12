import os
import dash
from functools import wraps
import datetime
import json
import dash_html_components as html
import dash_core_components as dcc
import plotly
from dash.dependencies import Input, Output
from flask import request

session_timestamps = []
session_mph = []
session_heartrate = []
latest_data = {"t": "N/A",
               "dyno_rpm": 0,
               "bike_rpm": 0,
               "bike_mph": 0,
               "heart_bpm": 0}

# Initialize the app
app = dash.Dash(__name__)
app.config.suppress_callback_exceptions = True
server = app.server  # This is the Flask Parent Server that we can use to receive webhooks

app.layout = html.Div(
    children=[
        html.Div(className='row',
                 children=
                 [
                     html.Div(className='four columns div-user-controls',
                              children=
                              [
                                  html.H1('blum.bike'),
                                  html.Div(id='live-update-text'),
                              ]
                              ),
                     html.Div(className='eight columns div-for-charts bg-grey',
                              children=
                              [
                                  dcc.Graph(id='live-update-graph'),
                                  dcc.Interval(
                                      id='interval-component',
                                      interval=1000,  # in milliseconds
                                      n_intervals=0
                                  )
                              ]
                              )
                 ]
                 )
    ]
)

# A decorator function to require an api key for pushing data to this application
# https://coderwall.com/p/4qickw/require-an-api-key-for-a-route-in-flask-using-only-a-decorator
def require_apikey(view_function):
    @wraps(view_function)
    # the new, post-decoration function. Note *args and **kwargs here.
    def decorated_function(*args, **kwargs):
        if request.json['apikey'] and request.json['apikey'] == str(os.environ.get("apikey")):
            #print("matched api key")
            return view_function(*args, **kwargs)
        else:
            print("invalid api key")
            return {"success": False}, 401
    return decorated_function


# Receive incoming data as POST JSON objects from the Particle Cloud
@server.route('/append', methods=['POST'])
@require_apikey
def append_data():
    global latest_data
    global session_timestamps
    global session_mph
    global session_heartrate
    latest_data = json.loads(request.json['data'])
    #print(latest_data)
    session_timestamps.append(datetime.datetime.fromtimestamp(int(latest_data['t'])))
    session_mph.append(latest_data['bike_mph'])
    session_heartrate.append(latest_data['heart_bpm'])
    #print(session_timestamps)
    #print(session_mph)
    #print("")
    return {"success": True}


@app.callback(Output('live-update-text', 'children'),
              [Input('interval-component', 'n_intervals')])
def update_metrics(n):
    global latest_data
    timestamp = datetime.datetime.fromtimestamp(int(latest_data['t'])).strftime('%c')
    style = {'padding': '5px', 'fontSize': '16px'}
    return [
        html.H2('Last Update: {}'.format(timestamp)),
        html.P('Dyno RPM: {0:0.2f} RPM'.format(latest_data['dyno_rpm']), style=style),
        html.P('Bike RPM: {0:0.2f} RPM'.format(latest_data['bike_rpm']), style=style),
        html.P('Bike Speed: {0:0.2f} MPM'.format(latest_data['bike_mph']), style=style),
        html.P('Heartrate: {0:0.2f} BPM'.format(latest_data['heart_bpm']), style=style)
    ]


# Multiple components can update every time interval gets fired.
@app.callback(Output('live-update-graph', 'figure'),
              [Input('interval-component', 'n_intervals')])
def update_graph_live(n):
    data = {
        'timestamp': session_timestamps,
        'speed': session_mph,
        'heartrate': session_heartrate,
    }

    # Create the graph with subplots
    fig = plotly.subplots.make_subplots(rows=2, cols=1, shared_xaxes=True, vertical_spacing=0.3, subplot_titles=("Bike Speed", "Heart Rate"))
    fig.update_layout(
        xaxis=dict(
            title_font=dict(
                size=14,
                color='grey',
            ),
            title_text="Time",
            zeroline=False,
            showline=False,
            showgrid=True,
            showticklabels=True,
            gridcolor='grey',
            ticks='outside',
            tickfont=dict(
                size=12,
                color='grey',
            ),
        ),
        xaxis2=dict(
            title_font=dict(
                size=14,
                color='grey',
            ),
            title_text="Time",
            zeroline=False,
            showline=False,
            showgrid=True,
            showticklabels=True,
            gridcolor='grey',
            ticks='outside',
            tickfont=dict(
                size=12,
                color='grey',
            ),
        ),
        yaxis=dict(
            title_font=dict(
                size=14,
                color='grey',
            ),
            title_text="Speed (mph)",
            zeroline=False,
            rangemode='nonnegative',
            showline=False,
            showgrid=True,
            showticklabels=True,
            gridcolor='grey',
            ticks='outside',
            tickfont=dict(
                size=12,
                color='grey',
            ),
        ),
        yaxis2=dict(
            title_font=dict(
                size=14,
                color='grey',
            ),
            title_text="Heart Rate (bpm)",
            zeroline=False,
            rangemode='nonnegative',
            showline=False,
            showgrid=True,
            showticklabels=True,
            gridcolor='grey',
            ticks='outside',
            tickfont=dict(
                size=12,
                color='grey',
            ),
        ),
        height=800,
        autosize=True,
        margin=dict(
            autoexpand=True,
            l=30,
            r=30,
            b=30,
            t=30,
        ),
        showlegend=False,
        plot_bgcolor='rgba(0,0,0,0)',
        paper_bgcolor='rgba(0,0,0,0)'
    )

    fig.append_trace({
        'x': data['timestamp'],
        'y': data['speed'],
        'text': data['speed'],
        'name': 'Bike Speed',
        'mode': 'lines+markers',
        'type': 'scatter',
        'line': dict(color='royalblue', width=4),
        'marker': dict(color='royalblue', size=12),
    }, 1, 1)
    fig.append_trace({
        'x': data['timestamp'],
        'y': data['heartrate'],
        'text': data['heartrate'],
        'name': 'Heart Rate',
        'mode': 'lines+markers',
        'type': 'scatter',
        'line': dict(color='firebrick', width=4),
        'marker': dict(color='firebrick', size=12),
    }, 2, 1)

    for i in fig['layout']['annotations']:
        i['font'] = dict(size=18, color='grey')

    return fig


if __name__ == '__main__':
    app.run_server(debug=True, port=8050)
