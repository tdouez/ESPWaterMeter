/* sav config */
function savButton() {
    console.log("Sav config");
    console.log($("#form_config").serialize());
    $.post('/config.htm', $("#form_config").serialize())
        .done( function(msg, textStatus, xhr) { 
            console.log($("#form_config").serialize());
            $.notify("Enregistrement effectué", "success");
        })
        .fail( function(xhr, textStatus, errorThrown) {
            $.notify("Enregistrement impossible "+ errorThrown, "error");
        })
        console.log("Form Submit config");	
}

/* gestion menu */
function openMenu(MenuName) {
    var i;
    var x = document.getElementsByClassName("menu");
    for (i = 0; i < x.length; i++) {
      x[i].style.display = "none";  
    }
    document.getElementById(MenuName).style.display = "block";  
}

/* fonction lecture json */
function lire_item_json(myjson) {
    console.log("lire item json");

    let j = JSON.parse(myjson, function(name, value) {
        if (name == "tps_maj") {
            $("#tps_maj").val(value);
        }
        if (name == "ntp_server") {
            $("#ntp_server").val(value);
        }
        if (name == "url_mqtt") {
            $("#url_mqtt").val(value);
        }
        if (name == "token_mqtt") {
            $("#token_mqtt").val(value);
        }
        if (name == "port_mqtt") {
            if (value==0) value=1883;
            $("#port_mqtt").val(value);
        }
        if (name == "user_mqtt") {
            $("#user_mqtt").val(value);
        }
        if (name == "pwd_mqtt") {
            $("#pwd_mqtt").val(value);
        }
        if (name == "module_name") {
            $("#module_name").val(value);
        } 
        if (name == "version") {
            document.getElementById("version").innerHTML = value;
        } 
        if (name == "pulse_factor") {
            $("#pulse_factor").val(value);
        } 
        if (name == "leak_threshold") {
            $("#leak_threshold").val(value);
        }
        if (name == "totalPulse") {
            document.getElementById("totalPulse").innerHTML = value;
        }
        if (name == "pulseCount") {
            document.getElementById("pulseCount").innerHTML = value;
        } 
        if (name == "flow") {
            document.getElementById("flow").innerHTML = value;
        } 
        if (name == "volume") {
            document.getElementById("volume").innerHTML = value;
        } 
        if (name == "volume_cumul") {
            document.getElementById("volume_cumul").innerHTML = value;
        }
        if (/^h[0-9]{1,2}/.test(name)) {
            document.getElementById(name).innerHTML = value;
        }
        if (name == "info_config") {
            affiche_info_config(value);
        }
        if (name == "erreur_config") {
            affiche_erreur_config(value);
        }
        if (name == "info") {
            affiche_info_info(value);
        }
        if (name == "erreur_info") {
            affiche_erreur_info(value);
        }
        if (name == "hour") {
            affiche_index(value);
        }
        if (name == "current_date") {
            document.getElementById("currentDate").innerHTML = value;
        }
        if (name == "startup_date") {
            document.getElementById("startDate").innerHTML = value;
        }
        if (name == "url_post") {
            $("#url_post").val(value);
        }
        if (name == "token_post") {
            $("#token_post").val(value);
        }
    });
}

/* affiche index sur tableau suivant heure */
function affiche_index(value) {
    if (value >= 0 && value <= 23) {
        for (i=0; i<=23; i++) {
            if (i == value) document.getElementById("h"+i).style.color = "red";
                else document.getElementById("h"+i).style.color = "initial";
        }
    }
}

/* affiche info sur panel info*/
function affiche_info_info(buffer) {
    document.getElementById("t_info").innerHTML = buffer;
    if (buffer == "") {
        document.getElementById("p_info").style.display = "none";
    }
    else {
        document.getElementById("p_info").style.display = "block";
    }
}

/* affiche erreur sur panel info*/
function affiche_erreur_info(buffer) {
    document.getElementById("t_erreur_info").innerHTML = buffer;
    if (buffer == "") {
        document.getElementById("p_erreur_info").style.display = "none";
    }
    else {
        document.getElementById("p_erreur_info").style.display = "block";
    }
}

/* affiche info config sur panel config*/
function affiche_info_config(buffer) {
    document.getElementById("t_info_config").innerHTML = buffer;
    if (buffer == "") {
        document.getElementById("p_info_config").style.display = "none";
    }
    else {
        document.getElementById("p_info_config").style.display = "block";
    }
}

/* affiche erreur config sur panel config*/
function affiche_erreur_config(buffer) {
    document.getElementById("t_erreur_config").innerHTML = buffer;
    if (buffer == "") {
        document.getElementById("p_erreur_config").style.display = "none";
    }
    else {
        document.getElementById("p_erreur_config").style.display = "block";
    }
}

/* mise a jour info module */
function maj_infos() { 
    console.log("maj infos");
        
    $.getJSON("/info.json", function(data) {
        let myNewJSON = JSON.stringify(data, null, '\t'); 
        lire_item_json(myNewJSON); 
    })
    .fail(function(xhr, textStatus, errorThrown) {
        console.log( "error " + errorThrown );
    });
}

/* init */
function init () {
    console.log("get /config.json");
    
    $.getJSON("/config.json", function(data) {
        let myNewJSON = JSON.stringify(data, null, '\t'); 
        lire_item_json(myNewJSON);     
    })
    .fail(function(xhr, textStatus, errorThrown) {
        console.log( "error " + errorThrown );
    });
    $.getJSON("/info.json", function(data) {
        let myNewJSON = JSON.stringify(data, null, '\t'); 
        lire_item_json(myNewJSON); 
    })
    .fail(function(xhr, textStatus, errorThrown) {
        console.log( "error " + errorThrown );
    });
}

window.onload=init();
timer = window.setInterval("maj_infos()", 5000);