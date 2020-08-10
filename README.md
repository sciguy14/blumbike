# BLUM.BIKE
This repo contains the code and documentation related to my blum.bike IOT bike trainer project. My instance of this project is publically accessible at [blum.bike](https://blum.bike), but you can use this code to deploy your own implementation.
  
This project is still a WORK-IN-PROGRESS!
  
Copyright 2020 [Jeremy Blum](https://www.jeremyblum.com), [Blum Idea Labs, LLC.](https://www.blumidealabs.com)  
This project is licensed under the MIT license (see LICENSE.md for details).

## Current Features
* Turn on the bike hardware to automatically start a session that is shown live in the web app.
* Web app shows a historical representation of heart rate and bike speed for the current training session, as well as the current real-time value (updated once per second).

## Repo Organization
* [blumbike_hardware](blumbike_hardware/): This folder includes the schematics, datasheets, and mechanical design files necessary for building the bike hardware.
* [blumbike_particle_firmware](blumbike_photon_firmware/): These are firmware files for flashing to the Particle Photon that powers the bike electronics.
* [blumbike_web_app](blumbike_web_app/): This the Python-based web app that is deployed to a Heroku Dyno.

## System Design
blum.bike has two main components: some cloud-connected simple electronics mounted to stationary bike, and a web app running on a Heroku free-tier dyno. They are connected via the Particle Cloud (a service the accompanies the Particle Photon microcontroller used in this project).

### Key Bike Hardware
* A road bike mounted on a stationary bike stand ([Amazon Link](https://amzn.to/3ezBoth)). A piece of black tape is added to the dyno. Its rotation in front of the optical sensor is used to compute dyno RPM.
* A [3D printed arm](blumbike_hardware/mechanical/breadboard_holder/) for mounting a full-length breadboard to the rear of the bike dyno. This part snaps in place, and is fixed in place with a hex standoff that inserts into one of the bolts on the Dyno.
* A full-length breadboard with the following main elements (see the Fritzing breadboard images in the [blumbike_hardware/schematics](blumbike_hardware/schematics/) folder and relevant datasheets in the [blumbike_hardware/datasheets](blumbike_hardware/datasheets/) folder).
    * A Particle Photon ([Documentation](https://docs.particle.io/datasheets/wi-fi/photon-datasheet)) cloud-connected microcontroller.
    * A QRD1114 optical sensor for measuring dyno RPM.
    * A comparator/hysteresis circuit based on an OPA344 Op-Amp for cleaning up the RPM measurement.
    * A Polar wireless heart rate receiver. This is attached via a wire harness to put it close to the bike seat.
    * A Polar T34 Chest strap heart rate sensor ([Amazon Link](https://amzn.to/2RPv1s9)).
    * A Trinamic TMC2209 Stepper Driver breakout board for driving a stepper motor that adjusts dyno resistance.
    * A L7805 Linear 5V regulator (plus bulk decoupling caps) for powering the logic elements from the 12V AC/DC wall adapter (12V is used for the Stepper Motor supply).
* A 12V, 1.5A AC/DC Wall adapter
* A NEMA-17 Stepper motor with a Planetary Gearbox ([Amazon Link](https://amzn.to/3emq3wr)) and 3D-printed adapter for controlling dyno resistance
* An endstop switch clipped onto the adjustable dyno with a 3D-printed bracket (so the stepper motor can home its resistance)

### Photon Firmware and Cloud
The Particle Photon runs some simple firmware that determines session start/stop time, heart rate, bike speed, etc. It publishes all the relevant data to the particle cloud once per second. The particle cloud is configred to fire a webhook to the python web app each time an update is received. A secret API key is inserted into the webhook contents, which is validated by the receiving web app.

### Python Web App Implementation
The web app is built as a python virtual env. It uses [Plotly Dash](https://dash.plotly.com/introduction) as the main mechanism for the UI and the graphs. It is deployed onto a [Heroku Free-Tier Dyno](https://www.heroku.com/pricing) and it leverages a [Heroku redis](https://elements.heroku.com/addons/heroku-redis) resource to maintain data for the duration of a session. To secure communication between the Particle cloud and the Heroku server, a matching API key is generated and stored in a Heroku environment variable for validating that the incoming webhook data from the Particle cloud is authentic. [Dash Bootstrap Components](https://dash-bootstrap-components.opensource.faculty.ai/) are used for the styling of the front-end.

## Instructions
This section is still a work in progress as I am still developing this project.
1. Fork this Repo to your own gitHub account and modify it as you desire.
2. 3D Print and assemble the mechanical elements. Build the circuit as described by the schematic. You will probably need to use a scope or a logic analyzer to get the alignment of the optical sensor working properly to generate consistent pulses on each dyno rotation.
3. Associate the Particle Photon to your Wi-Fi network and to your Particle Cloud account.
4. Use the Particle Dev IDE to flash the firmware onto the Photon via the cloud, or copy the firmware into their cloud IDE and flash it from there. Ensure that you see events streaming into the Particle Cloud interface for your device.
5. On the Particle Cloud Integration page, setup a new Webhook. Choose to create a custom template and populate it as follows (replacing the <> items accordingly):
    ```JSON
    {
        "event": "bike_data",
        "deviceID": "<YOUR_DEVICE_ID>",
        "url": "https://<YOUR_HEROKU_URL>/update",
        "requestType": "POST",
        "noDefaults": true,
        "rejectUnauthorized": true,
        "json": {
            "event": "{{{PARTICLE_EVENT_NAME}}}",
            "data": "{{{PARTICLE_EVENT_VALUE}}}",
            "apikey": "<YOUR_GENERATED_API_KEY>"
        }
    }
    ```  
    The API key can be whatever you want. You will just need to use the same key when you setup the heroku environment variables.
6. Create a Heroku Account, and spin up a free-tier dyno.
7. Choose to "Deploy from GitHub" and connect it to your GitHub account. Point it at the repo that you forked to your account.
8. On the "Settings" page for the dyno, add the following "Config Vars":  
    `apikey` = `<YOUR_GENERATED_API_KEY>` - this is the same API key you configured in the JSON webhook.  
    `PROJECT_PATH` = `blumbike_web_app` - this will make the Heroku automatic GitHub deployment look in the right subfolder of this repo for the application ([more info about this](https://stackoverflow.com/a/53221996)).
9. Add a buildpack that will ensure the right subdirectory is used for automatic deployment. Add https://github.com/timanovsky/subdir-heroku-buildpack.git to the buildpack list and drag it to the top of the list (above python).
10. On the "Resources" page for your Dyno add a Heroku Redis instance. This should automatically add a "Config Var" with your `REDIS_URL`. This app will use this redis store to hold your data.
11. On the "Deploy" page, choose to deploy your master branch. Future deploys will happen automatically each time you push local changes to your upstream master branch.

## Doing Local Development
It's impractical to re-deploy to Heroku for every software change that you want to test. I recommend the following local development environment:  
I like using [PyCharm](https://www.jetbrains.com/pycharm/) for development of the Python web app, and I used [Particle Dev](https://docs.particle.io/tutorials/developer-tools/dev/) to write and deploy firmware to the Particle Photon.
  
[Ngrok](https://ngrok.com/) can be used to pipe the Photon webhooks to the localhost development server that you'll be running on your local machine. Create an account, and install it on your development machine. Once installed, launch the service with `ngrok http 8050`. This will give you a forwarding URL of the format `https://<UNIQUE ID>.ngrok.io`. You can also see this URL in your ngrok dashboard at [dashboard.ngrok.com](https://dashboard.ngrok.com/status/tunnels). Temporarily edit your Particle webhook event to use this URL instead of the heroku URL. So for example, you would cheange `https://my-app.herokuapp.com/update` to `https://abcde12345.ngrok.io/update`. You should be able to see the webhook requests come into this termainal window as soon as you point the Particle webhook at this address. You will see "502 Bad Gateway Errors" until you actually launch your local development server from PyCharm.
  
Create a new PyCharm project in the `blumbike_web_app` folder. Set up the Virtual Env using the requirements.txt file (PyCharm should prompt you to install the right things). Create a Run configuration pointed at app.py and using the Virtual Env interpreter. Add the following environment variables ([instructions](https://stackoverflow.com/questions/42708389/how-to-set-environment-variables-in-pycharm/42708480#42708480)):
* `mode` = `dev` - This will configure the server to run on local port 8050, which is what you used to setup ngrok.
* `apikey` = `<PUT_YOUR_API_KEY_HERE>` - This is the same apikey that you added to the particle webhook JSON message and to heroku.
* `REDIS_URL` = `<PUT_YOUR_REDIS_URL_HERE>` - You can use the same REDIS URL that is used by Heroku, or you can run a seperate development redis instance on your local machine and point at that.
  
Run the PyCharm configuration that you setup, and the ngrok instance should start showing "200 OK" responses. Visit [localhost:8050/](http://localhost:8050/) in your webbrowser to see your local instance of the web app. You can see more details about the webhooks coming into your machine via the ngrok inspection interface at [localhost:4040/inspect/http](http://localhost:8050/).

## Other Useful References
The following are some use websites that I referenced while working on this project. You might find them useful as well:
* [Curing Comparator Instability with Hysteresis](https://www.analog.com/en/analog-dialogue/articles/curing-comparator-instability-with-hysteresis.html#)
* [Inverting Schmitt Trigger Calculator](https://www.random-science-tools.com/electronics/inverting-schmitt-trigger-calculator.htm)
* [Using an Op-amp as a Comparator](https://www.electronics-tutorials.ws/opamp/op-amp-comparator.html)
* [QRD1114 Optical Detector Hookup Guide](https://learn.sparkfun.com/tutorials/qrd1114-optical-detector-hookup-guide/all)
* [Particle Photon API Reference](https://docs.particle.io/reference/device-os/firmware/photon/#cloud-functions)
* [Plotly Dash Documentation](https://dash.plotly.com/dash-core-components)
  
  
_Disclaimers: As an Amazon Associate, I earn from qualifying purchases if you purchase items via Amazon.com links in this README. I also assume no liability if you manage to injure yourself while using a stationary bike._