package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class CollabAwarenessEvent extends ServerEvent {
    private String roomId;
    private String senderSessionId;
    private String awareness;

    public CollabAwarenessEvent() {
        super("collab.awareness");
    }

    public CollabAwarenessEvent(String roomId, String senderSessionId, String awareness) {
        super("collab.awareness");
        this.roomId = roomId;
        this.senderSessionId = senderSessionId;
        this.awareness = awareness;
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }

    public String getSenderSessionId() {
        return senderSessionId;
    }

    public void setSenderSessionId(String senderSessionId) {
        this.senderSessionId = senderSessionId;
    }

    public String getAwareness() {
        return awareness;
    }

    public void setAwareness(String awareness) {
        this.awareness = awareness;
    }
}
