package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

@JsonIgnoreProperties(ignoreUnknown = true)
public class CollabUpdateMessage extends ClientMessage {
    private String update;

    public CollabUpdateMessage() {
        setType("collab.update");
    }

    public CollabUpdateMessage(String update) {
        setType("collab.update");
        this.update = update;
    }

    public String getUpdate() {
        return update;
    }

    public void setUpdate(String update) {
        this.update = update;
    }
}
