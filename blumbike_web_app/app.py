# blum.bike Web App
# Copyright 2020 Jeremy Blum, Blum Idea Labs, LLC.
# www.jeremyblum.com
# This code is licensed under MIT license (see LICENSE.md for details)

import os
import redis
import dash
from functools import wraps
import datetime
import timeago
import json
import dash_html_components as html
import dash_core_components as dcc
import plotly
from dash.dependencies import Input, Output
from flask import request

# Initialize the app
app = dash.Dash(__name__)
app.config.suppress_callback_exceptions = True
server = app.server  # This is the Flask Parent Server that we can use to receive webhooks

# Connect to Redis for persistent storage of session data
r = redis.from_url(os.environ.get("REDIS_URL"), decode_responses=True)

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
                                  dcc.Graph(id='live-update-graph', config={'displayModeBar': False}),
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

    # The "event" key will be "start_session" for a new session, or "new_data" for new data
    if latest_data['event'] == "start_session":
        # When we're ready to start a new session (photon switched on), we clear the existing redis data
        r.flushdb()
        # Note when this session started
        r.set("session_start", latest_data['t'])
        print("STARTED A NEW SESSION: {}".format(latest_data))
        return {"reply": "started session"}

    # If a value comes in out of order, discard it.
    if r.exists('timestamp') and int(r.lindex('timestamp', 0)) > int(latest_data['t']):
        print("IGNORED (STALE): {}".format(latest_data))
        return {"reply": "ignored stale data"}

    # Push the data into a running list in redis
    r.lpush('timestamp', latest_data['t'])
    r.lpush('bike_mph', latest_data['bike_mph'])
    r.lpush('heart_bpm', latest_data['heart_bpm'])

    # Keep the list trimmed
    r.ltrim('timestamp', 0, 300)
    r.ltrim('bike_mph', 0, 300)
    r.ltrim('heart_bpm', 0, 300)

    print("APPENDED: {}".format(latest_data))
    return {"reply": "data appended"}


@app.callback(Output('live-update-text', 'children'),
              [Input('interval-component', 'n_intervals')])
def update_metrics(n):
    if r.exists('session_start') and r.exists('timestamp'):
        return [
            html.H3('Session started: {}'.format(timeago.format(datetime.datetime.fromtimestamp(int(r.get('session_start')))), datetime.datetime.now())),
            html.P('Last Update: {}'.format(datetime.datetime.fromtimestamp(int(r.lindex('timestamp', 0))).strftime('%c'))),
            html.P('Bike Speed: {0:0.2f} MPH'.format(float(r.lindex('bike_mph', 0)))),
            html.P('Heart Rate: {0:0.2f} BPM'.format(float(r.lindex('heart_bpm', 0))))
        ]
    style = {'fontStyle': 'italic'}
    return [
        html.P('Waiting to receive data from bike...', style=style)
    ]


# Multiple components can update every time interval gets fired.
@app.callback(Output('live-update-graph', 'figure'),
              [Input('interval-component', 'n_intervals')])
def update_graph_live(n):
    # Create the graph with subplots
    fig = plotly.subplots.make_subplots(rows=2, cols=1, shared_xaxes=True, vertical_spacing=0.3, subplot_titles=("Bike Speed", "Heart Rate"))
    fig.update_layout(
        xaxis=dict(
            fixedrange=True,
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
            fixedrange=True,
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
            fixedrange=True,
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
            fixedrange=True,
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

    for i in fig['layout']['annotations']:
        i['font'] = dict(size=18, color='grey')

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
            'line': dict(color='royalblue', width=2),
            'marker': dict(color='royalblue', size=6),
        }, 1, 1)
        fig.append_trace({
            'x': data['timestamp'],
            'y': data['heartrate'],
            'text': data['heartrate'],
            'name': 'Heart Rate',
            'mode': 'lines+markers',
            'type': 'scatter',
            'line': dict(color='firebrick', width=2),
            'marker': dict(color='firebrick', size=6),
        }, 2, 1)

    return fig


if __name__ == '__main__':
    if "mode" in os.environ and str(os.environ.get("mode")) == "dev":
        app.run_server(debug=True, port=8050)
    else:
        app.run_server(host='0.0.0.0')
