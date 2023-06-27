## drone-ci docker jizz

To rebuild all ci images and push them to the sispop registry server do:

    $ docker login registry.sispop.rocks
    $ ./rebuild-docker-images.py

If you aren't part of the Sispop team, you'll likely need to set up your own registry and change
registry.sispop.rocks to your own domain name in order to do anything useful with this.
