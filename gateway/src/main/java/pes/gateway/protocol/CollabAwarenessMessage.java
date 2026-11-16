package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

@JsonIgnoreProperties(ignoreUnknown = true)
public class CollabAwarenessMessage extends ClientMessage {
    private String awareness;

    public CollabAwarenessMessage() {
        setType("collab.awareness");
    }

    public CollabAwarenessMessage(String awareness) {
        setType("collab.awareness");
        this.awareness = awareness;
    }

    public String getAwareness() {
        return awareness;
    }

    public void setAwareness(String awareness) {
        this.awareness = awareness;
    }
}
