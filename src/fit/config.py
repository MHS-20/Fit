import configparser
import os


def fitconfig_read():
    xdg_config_home = (
        os.environ["XDG_CONFIG_HOME"]
        if "XDG_CONFIG_HOME" in os.environ
        else "~/.config"
    )
    configfiles = [
        os.path.expanduser(os.path.join(xdg_config_home, "fit/config")),
        os.path.expanduser("~/.fitconfig"),
    ]
    config = configparser.ConfigParser()
    config.read(configfiles)
    return config


def fitconfig_user_get(config):
    if "user" in config:
        if "name" in config["user"] and "email" in config["user"]:
            return f"{config['user']['name']} <{config['user']['email']}>"
    return None
