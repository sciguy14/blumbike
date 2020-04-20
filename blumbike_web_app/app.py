# blum.bike Web App
# Copyright 2020 Jeremy Blum, Blum Idea Labs, LLC.
# www.jeremyblum.com
# This code is licensed under MIT license (see LICENSE.md for details)

import os
import redis
import dash
import time
from functools import wraps
import datetime
from natural import date
import json
import dash_bootstrap_components as dbc
import dash_html_components as html
import dash_core_components as dcc
import plotly
from dash.dependencies import Input, Output
from flask import request

# Initialize the app
app = dash.Dash(__name__, external_stylesheets=[dbc.themes.SOLAR])
app.title = "blum.bike"
app.config.suppress_callback_exceptions = True
server = app.server  # This is the Flask Parent Server that we can use to receive webhooks

# Connect to Redis for persistent storage of session data
r = redis.from_url(os.environ.get("REDIS_URL"), decode_responses=True)

sidebar =   dbc.Col(
                html.Div([
                    html.H3("blum.bike", className="card-header"),
                    html.Div(id='live-update-text', className="card-body")
                ], className='card mb-3'),
                className='col-md-12 col-lg-4 sidebar'
            )

content =   dbc.Col(className='col-md-12 col-lg-8 col-lg-offset-4 main',
                    children=[
                        html.Div(id='graph-spinner', style={'text-align': 'center'},
                                 children=[
                                     dbc.Spinner(color="primary"),
                                     html.P('Loading Graphs...')
                                     ]
                                 ),
                        html.Div(id='live-graph-div', style={'visibility': 'hidden'}, # Starts Hidden so the Graph can load first
                                 children=[
                                    dcc.Graph(id='live-update-graph', config={'displayModeBar': False})
                                 ]),
                        dcc.Interval(id='interval-component', interval=1000, n_intervals=0)
                    ])

app.layout = dbc.Container(dbc.Row([sidebar, content]), style={'padding': '15px'}, fluid=True)


# A decorator function to require an api key for pushing data to this application
# https://coderwall.com/p/4qickw/require-an-api-key-for-a-route-in-flask-using-only-a-decorator
def require_apikey(view_function):
    @wraps(view_function)
    # the new, post-decoration function. Note *args and **kwargs here.
    def decorated_function(*args, **kwargs):
        if request.json['apikey'] and "apikey" in os.environ and request.json['apikey'] == str(os.environ.get("apikey")):
            return view_function(*args, **kwargs)
        else:
            print("invalid api key match")
            return {"reply": "invalid key"}, 401
    return decorated_function


# Receive incoming data as POST JSON objects from the Particle Cloud
@server.route('/update', methods=['POST'])
@require_apikey
def rest_update():
    latest_data = json.loads(request.json['data'])

    # The "event" key will be:
    # "powered_on" when the Photon is turned on
    # "start_session" when the Photon has detected that a new session has started
    # "end_session" when the Photon has detected that a session has ended
    # "new_data" for new bike stats
    if latest_data['event'] == "powered_on":
        # This triggers when the photon is powered on
        # Note when this session started
        r.set("powered_on", latest_data['t'])
        print("BIKE POWERED ON: {}".format(latest_data))
        return {"reply": "power on received"}

    if latest_data['event'] == "start_session":
        # When user has initiated a new session (sequential non-zero dyno RPMs), we clear all existing redis data
        r.flushdb()
        # Note when this session started
        r.set("session_start", latest_data['t'])
        print("STARTED A NEW SESSION: {}".format(latest_data))
        return {"reply": "started session"}

    if latest_data['event'] == "end_session":
        # When user has finished a new session (sequential non-zero dyno RPMs), we can note that in the UI
        r.set("session_end", latest_data['t'])
        time.sleep(.1)  # Briefly sleep after the end of a session to ensure the session end is set in redis
        print("ENDED THE SESSION: {}".format(latest_data))
        return {"reply": "ended session"}

    elif latest_data['event'] == "new_data":
        # If a value comes in out of order, discard it.
        if (r.exists('timestamp') and int(r.lindex('timestamp', 0)) > int(latest_data['t'])) or r.exists('session_end'):
            print("IGNORED (STALE): {}".format(latest_data))
            return {"reply": "ignored stale data"}

        # Push the data into a running list in redis
        r.lpush('timestamp', latest_data['t'])
        r.lpush('bike_mph', latest_data['bike_mph'])
        r.lpush('heart_bpm', latest_data['heart_bpm'])

        # Keep the list trimmed (disabled. Shouldn't be necessary to trim the list since we end sessions when the bike stops).
        # r.ltrim('timestamp', 0, 300)
        # r.ltrim('bike_mph', 0, 300)
        # r.ltrim('heart_bpm', 0, 300)

        print("APPENDED: {}".format(latest_data))
        return {"reply": "data appended"}

    else:
        print("APPENDED: {}".format(latest_data))
        return {"reply": "event '{}' not understood".format(latest_data['event'])}, 501


@app.callback(Output('live-update-text', 'children'),
              [Input('interval-component', 'n_intervals')])
def update_metrics(n):

    if r.exists('session_end') and r.exists('timestamp'):
        start_datetime = datetime.datetime.fromtimestamp(int(r.get('session_start')))
        end_datetime = datetime.datetime.fromtimestamp(int(r.get('session_end')))
        speed_readings = [float(i) for i in r.lrange('bike_mph', 0, -1)]
        heart_readings = [float(i) for i in r.lrange('heart_bpm', 0, -1)]
        if len(speed_readings) > 0 and len(heart_readings) > 0:
            return [
                html.H5('Last session ended: {}'.format(date.duration(end_datetime)), className='card-title'),
                html.P('Session Duration: {}'.format(date.delta(start_datetime, end_datetime)[0]), className='card-text'),
                html.Br(),
                html.P('Session Average Bike Speed: {0:0.2f} MPH'.format(sum(speed_readings)/len(speed_readings)), className='card-text'),
                html.P('Session Max Bike Speed: {0:0.2f} MPH'.format(max(speed_readings)), className='card-text'),
                html.Br(),
                html.P('Session Average Heart Rate: {0:0.2f} BPM'.format(sum(heart_readings)/len(heart_readings)), className='card-text'),
                html.P('Session Max Heart Rate: {0:0.2f} BPM'.format(max(heart_readings)), className='card-text')
            ]
    elif r.exists('session_start') and r.exists('timestamp'):
        start_datetime = datetime.datetime.fromtimestamp(int(r.get('session_start')))
        return [
            html.H5('Current session started: {}'.format(date.duration(start_datetime)), className='card-title'),
            html.P('Last Update: {}'.format(datetime.datetime.fromtimestamp(int(r.lindex('timestamp', 0))).strftime('%c')), className='card-text'),
            html.Br(),
            html.P('Current Bike Speed: {0:0.2f} MPH'.format(float(r.lindex('bike_mph', 0))), className='card-text'),
            html.P('Current Heart Rate: {0:0.2f} BPM'.format(float(r.lindex('heart_bpm', 0))), className='card-text')
        ]
    style = {'fontStyle': 'italic'}
    return [
        html.P('Waiting to receive data from bike...', className='card-text', style=style)
    ]


# Multiple components can update every time interval gets fired.
@app.callback([Output('live-update-graph', 'figure'), Output('graph-spinner', 'style'), Output('live-graph-div', 'style')],
              [Input('interval-component', 'n_intervals')])
def update_graph_live(n):
    # Create the graph with subplots
    fig = plotly.subplots.make_subplots(rows=2, cols=1, shared_xaxes=True, vertical_spacing=0.3, subplot_titles=("Bike Speed", "Heart Rate"))
    fig.update_layout(
        xaxis=dict(
            fixedrange=True,
            title_font=dict(
                size=14,
                color='#839496',
            ),
            title_text="Time",
            zeroline=False,
            showline=False,
            showgrid=True,
            showticklabels=True,
            gridcolor='#839496',
            ticks='outside',
            tickfont=dict(
                size=12,
                color='#839496',
            ),
        ),
        xaxis2=dict(
            fixedrange=True,
            title_font=dict(
                size=14,
                color='#839496',
            ),
            title_text="Time",
            zeroline=False,
            showline=False,
            showgrid=True,
            showticklabels=True,
            gridcolor='#839496',
            ticks='outside',
            tickfont=dict(
                size=12,
                color='#839496',
            ),
        ),
        yaxis=dict(
            fixedrange=True,
            title_font=dict(
                size=14,
                color='#839496',
            ),
            title_text="Speed (mph)",
            zeroline=False,
            rangemode='nonnegative',
            showline=False,
            showgrid=True,
            showticklabels=True,
            gridcolor='#839496',
            ticks='outside',
            tickfont=dict(
                size=12,
                color='#839496',
            ),
        ),
        yaxis2=dict(
            fixedrange=True,
            title_font=dict(
                size=14,
                color='#839496',
            ),
            title_text="Heart Rate (bpm)",
            zeroline=False,
            rangemode='nonnegative',
            showline=False,
            showgrid=True,
            showticklabels=True,
            gridcolor='#839496',
            ticks='outside',
            tickfont=dict(
                size=12,
                color='#839496',
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

    for i in fig['layout']['annotations']:
        i['font'] = dict(size=18, color='#839496')

    if r.exists('timestamp'):
        data = {
            'timestamp': [datetime.datetime.fromtimestamp(int(x)) for x in r.lrange('timestamp', 0, -1)],
            'speed': [float(i) for i in r.lrange('bike_mph', 0, -1)],
            'heartrate': [float(i) for i in r.lrange('heart_bpm', 0, -1)]
        }
        fig.append_trace({
            'x': data['timestamp'],
            'y': data['speed'],
            'text': data['speed'],
            'name': 'Bike Speed',
            'mode': 'lines+markers',
            'type': 'scatter',
            'line': dict(color='#268BD2', width=2),
            'marker': dict(color='#268BD2', size=6),
        }, 1, 1)
        fig.append_trace({
            'x': data['timestamp'],
            'y': data['heartrate'],
            'text': data['heartrate'],
            'name': 'Heart Rate',
            'mode': 'lines+markers',
            'type': 'scatter',
            'line': dict(color='#fd7e14', width=2),
            'marker': dict(color='#fd7e14', size=6),
        }, 2, 1)

    return fig, {'display': 'none'}, {'visibility': 'visible'}


if __name__ == '__main__':
    if "mode" in os.environ and str(os.environ.get("mode")) == "dev":
        app.run_server(debug=True, port=8050)
    else:
        app.run_server(host='0.0.0.0')
