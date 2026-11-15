package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

@JsonIgnoreProperties(ignoreUnknown = true)
public class RoomJoinMessage extends ClientMessage {
    private String roomId;
    private String userId;

    public RoomJoinMessage() {
        setType("room.join");
    }

    public RoomJoinMessage(String roomId, String userId) {
        setType("room.join");
        this.roomId = roomId;
        this.userId = userId;
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }

    public String getUserId() {
        return userId;
    }

    public void setUserId(String userId) {
        this.userId = userId;
    }
}
