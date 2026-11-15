package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class RoomLeftEvent extends ServerEvent {
    private String roomId;
    private String sessionId;

    public RoomLeftEvent() {
        super("room.left");
    }

    public RoomLeftEvent(String roomId, String sessionId) {
        super("room.left");
        this.roomId = roomId;
        this.sessionId = sessionId;
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }

    public String getSessionId() {
        return sessionId;
    }

    public void setSessionId(String sessionId) {
        this.sessionId = sessionId;
    }
}
